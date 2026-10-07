#include "QcServer.h"

#include "QcConnection.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcTransport.h"

#include <QLoggingCategory>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcServer, "qtcraft.server")

// Inputs a client may queue beyond one per tick before further inputs are dropped (anti speed-hack).
constexpr int maxInputBudget = 10;
constexpr int tickIntervalMs = int(QcPhysics::tickDuration * 1000.0);
// Bounded so that turning around re-prioritises quickly instead of finishing a long old queue.
constexpr qsizetype maxPendingGeneration = 48;

QcChunkPos chunkOf(const QcVec3 &position)
{
    return QcBlockPos{int(std::floor(position.x)), 0, int(std::floor(position.z))}.chunk();
}

} // namespace

struct QcServer::Client
{
    enum class State { AwaitingHello, Playing, Closed };

    std::unique_ptr<QcConnection> connection;
    State state = State::AwaitingHello;
    quint32 entityId = 0;
    QString name;
    QcEntityMotion motion;
    int inputBudget = maxInputBudget;
    quint32 lastInputSequence = 0;
    bool stateDirty = false;
    QSet<QcChunkPos> sentChunks;
};

QcServer::QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                   std::unique_ptr<QcWorldGenerator> generator, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_content(content)
    , m_world(std::move(content), std::move(generator))
    , m_tickTimer(new QTimer(this))
{
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    m_tickTimer->setInterval(tickIntervalMs);
    connect(m_tickTimer, &QTimer::timeout, this, &QcServer::tick);
}

QcServer::~QcServer() = default;

void QcServer::start()
{
    m_tickTimer->start();
}

void QcServer::stop()
{
    m_tickTimer->stop();
    for (const auto &client : m_clients) {
        client->connection->send(QcMsg::Disconnect{u"Server stopped"_s});
        client->connection->close();
    }
}

void QcServer::addClient(std::unique_ptr<QcTransport> transport)
{
    auto client = std::make_unique<Client>();
    Client *raw = client.get();
    client->connection = std::make_unique<QcConnection>(std::move(transport), QcProtocol::Peer::Client,
                                                        [this, raw](QcMessage &&message) { handleMessage(*raw, std::move(message)); });
    connect(client->connection.get(), &QcConnection::closed, this, [raw] { raw->state = Client::State::Closed; });
    m_clients.push_back(std::move(client));
    emit clientCountChanged(clientCount());
}

void QcServer::handleMessage(Client &client, QcMessage &&message)
{
    if (client.state == Client::State::Closed) {
        return;
    }
    if (const auto *hello = std::get_if<QcMsg::Hello>(&message)) {
        handleHello(client, *hello);
        return;
    }
    if (client.state != Client::State::Playing) {
        client.connection->send(QcMsg::Disconnect{u"Expected Hello"_s});
        client.connection->close();
        return;
    }
    if (const auto *input = std::get_if<QcMsg::PlayerInput>(&message)) {
        handleInput(client, *input);
    } else if (std::holds_alternative<QcMsg::Disconnect>(message)) {
        client.connection->close();
    } else {
        qCWarning(lcServer) << "Ignoring unexpected message type" << message.index() << "from" << client.name;
    }
}

void QcServer::handleHello(Client &client, const QcMsg::Hello &hello)
{
    if (client.state != Client::State::AwaitingHello) {
        return;
    }
    if (hello.protocolVersion != QcProtocol::version) {
        client.connection->send(QcMsg::Disconnect{
            u"Protocol version mismatch: server %1, client %2"_s.arg(QcProtocol::version).arg(hello.protocolVersion)});
        client.connection->close();
        return;
    }
    client.state = Client::State::Playing;
    client.entityId = m_nextEntityId++;
    client.name = hello.playerName.left(32);
    QcBlockPos spawn = m_world.generator().spawnPoint();
    if (m_config.spawnColumn) {
        spawn = {(*m_config.spawnColumn)[0], 0, (*m_config.spawnColumn)[1]};
    }
    // Stand on whatever the generated column really contains (caves, trees, water surface).
    const std::shared_ptr<const QcChunk> spawnChunk = m_world.loadChunk(spawn.chunk());
    spawn.y = spawnChunk->highestNonAir(spawn.localX(), spawn.localZ()) + 1;
    client.motion.position = {spawn.x + 0.5, double(spawn.y), spawn.z + 0.5};
    client.stateDirty = true;
    qCInfo(lcServer) << "Player" << client.name << "joined as entity" << client.entityId;

    client.connection->send(QcMsg::Welcome{QcProtocol::version, client.entityId, m_config.worldName, m_config.seed});
    client.connection->send(QcMsg::ContentSync{m_content});
    streamChunks(client);
}

void QcServer::handleInput(Client &client, const QcMsg::PlayerInput &message)
{
    if (client.inputBudget <= 0) {
        return;
    }
    --client.inputBudget;
    // The decoder already rejects non-finite values; ranges are enforced here.
    QcPlayerInput input = message.input;
    input.forward = std::clamp(input.forward, -1.0f, 1.0f);
    input.strafe = std::clamp(input.strafe, -1.0f, 1.0f);
    input.pitch = std::clamp(input.pitch, -90.0f, 90.0f);
    input.yaw = std::fmod(input.yaw, 360.0f);
    QcPhysics::stepPlayer(client.motion, input, m_world, QcPhysics::tickDuration);
    client.lastInputSequence = input.sequence;
    client.stateDirty = true;
}

void QcServer::streamChunks(Client &client)
{
    const QcChunkPos centre = chunkOf(client.motion.position);
    const int radius = m_config.viewDistance;

    // Unload with one chunk of hysteresis so walking along a border does not thrash.
    for (auto it = client.sentChunks.begin(); it != client.sentChunks.end();) {
        if (qcChunkDistance(*it, centre) > radius + 1) {
            client.connection->send(QcMsg::ChunkUnload{*it});
            it = client.sentChunks.erase(it);
        } else {
            ++it;
        }
    }

    QList<QcChunkPos> missing;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            if (!client.sentChunks.contains(pos)) {
                missing.append(pos);
            }
        }
    }
    std::ranges::sort(missing, [centre](QcChunkPos a, QcChunkPos b) {
        const auto distance = [centre](QcChunkPos p) {
            return (p.x - centre.x) * (p.x - centre.x) + (p.z - centre.z) * (p.z - centre.z);
        };
        return distance(a) < distance(b);
    });
    int sent = 0;
    for (const QcChunkPos &pos : std::as_const(missing)) {
        if (const std::shared_ptr<const QcChunk> chunk = m_world.chunk(pos)) {
            if (sent < m_config.chunksPerTick) {
                client.connection->send(QcMsg::ChunkData{chunk});
                client.sentChunks.insert(pos);
                ++sent;
            }
        } else if (m_world.pendingGenerationCount() < maxPendingGeneration) {
            // Nearest first: the list is sorted by distance and the pool works in FIFO order.
            m_world.requestChunk(pos);
        }
    }
}

void QcServer::tick()
{
    ++m_tickCount;
    removeClosedClients();
    m_world.collectGeneratedChunks();
    for (const auto &client : m_clients) {
        if (client->state != Client::State::Playing) {
            continue;
        }
        client->inputBudget = std::min(client->inputBudget + 1, maxInputBudget);
        streamChunks(*client);
        if (client->stateDirty) {
            client->connection->send(QcMsg::PlayerState{client->lastInputSequence, client->motion});
            client->stateDirty = false;
        }
    }

    // Drop chunks no player needs any more.
    // Keep everything within streaming range of a player, sent or not yet sent.
    QSet<QcChunkPos> needed;
    for (const auto &client : m_clients) {
        needed.unite(client->sentChunks);
        const QcChunkPos centre = chunkOf(client->motion.position);
        for (int dx = -m_config.viewDistance; dx <= m_config.viewDistance; ++dx) {
            for (int dz = -m_config.viewDistance; dz <= m_config.viewDistance; ++dz) {
                needed.insert({centre.x + dx, centre.z + dz});
            }
        }
    }
    const QList<QcChunkPos> loaded = m_world.loadedChunks();
    for (const QcChunkPos &pos : loaded) {
        if (!needed.contains(pos)) {
            m_world.unloadChunk(pos);
        }
    }
}

void QcServer::removeClosedClients()
{
    const auto removed = std::erase_if(m_clients, [](const std::unique_ptr<Client> &client) {
        return client->state == Client::State::Closed;
    });
    if (removed > 0) {
        emit clientCountChanged(clientCount());
    }
}
