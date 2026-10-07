#include "QcServer.h"

#include "QcConnection.h"
#include "QcInventory.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcRaycast.h"
#include "QcTransport.h"

#include <QDir>
#include <QFile>
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
    QcInventory inventory;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool joined = false;
    std::optional<QcBlockPos> miningPos;
    quint64 miningStartTick = 0;
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

QcStatus QcServer::openWorld()
{
    if (m_config.worldDirectory.isEmpty()) {
        return {};
    }
    m_storage.emplace(m_config.worldDirectory, m_content);
    if (!QFile::exists(QDir(m_config.worldDirectory).filePath(u"world.json"_s))) {
        m_info.name = m_config.worldName;
        m_info.seed = m_config.seed;
        m_info.created = QDateTime::currentDateTimeUtc();
        m_info.lastPlayed = m_info.created;
        return m_storage->writeInfo(m_info);
    }
    QcResult<QcWorldInfo> info = m_storage->readInfo();
    if (!info) {
        return qcError(info.error());
    }
    if (info->seed != m_config.seed) {
        return qcError(u"World seed %1 does not match the generator seed %2"_s.arg(info->seed).arg(m_config.seed));
    }
    m_info = *info;
    m_config.worldName = m_info.name;
    QStringList warnings;
    QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> chunks = m_storage->readChunks(warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcServer) << warning;
    }
    if (!chunks) {
        return qcError(chunks.error());
    }
    m_world.adoptStoredChunks(*chunks);
    qCInfo(lcServer) << "Opened world" << m_info.name << "with" << chunks->size() << "stored chunks";
    return {};
}

void QcServer::savePlayer(const Client &client)
{
    if (!m_storage || client.name.isEmpty()) {
        return;
    }
    QcPlayerData player;
    player.motion = client.motion;
    player.yaw = client.yaw;
    player.pitch = client.pitch;
    player.inventory = client.inventory;
    if (const QcStatus saved = m_storage->writePlayer(client.name, player); !saved) {
        qCWarning(lcServer) << saved.error();
    }
}

QcStatus QcServer::save()
{
    if (!m_storage) {
        return {};
    }
    m_info.lastPlayed = QDateTime::currentDateTimeUtc();
    if (const QcStatus written = m_storage->writeInfo(m_info); !written) {
        return written;
    }
    const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks = m_world.takeChunksToSave();
    if (const QcStatus written = m_storage->writeChunks(chunks); !written) {
        // Keep the regions dirty so the next save tries again.
        m_world.markDirty(chunks.keys());
        return written;
    }
    for (const auto &client : m_clients) {
        if (client->joined) {
            savePlayer(*client);
        }
    }
    return {};
}

QcStatus QcServer::stop()
{
    m_tickTimer->stop();
    const QcStatus saved = save();
    if (!saved) {
        qCCritical(lcServer) << "Saving failed:" << saved.error();
    }
    for (const auto &client : m_clients) {
        client->connection->send(QcMsg::Disconnect{u"Server stopped"_s});
        client->connection->close();
    }
    return saved;
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
    } else if (const auto *start = std::get_if<QcMsg::StartMining>(&message)) {
        client.miningPos = start->pos;
        client.miningStartTick = m_tickCount;
    } else if (std::holds_alternative<QcMsg::CancelMining>(message)) {
        client.miningPos.reset();
    } else if (const auto *finish = std::get_if<QcMsg::FinishMining>(&message)) {
        handleFinishMining(client, finish->pos);
    } else if (const auto *use = std::get_if<QcMsg::UseItem>(&message)) {
        handleUseItem(client, *use);
    } else if (const auto *select = std::get_if<QcMsg::SelectHotbarSlot>(&message)) {
        client.inventory.setSelectedHotbarSlot(select->slot);
        client.miningPos.reset();
        sendInventory(client);
    } else if (const auto *move = std::get_if<QcMsg::MoveInventoryItem>(&message)) {
        client.inventory.move(move->from, move->to, *m_content);
        client.miningPos.reset();
        sendInventory(client);
    } else if (const auto *chat = std::get_if<QcMsg::ChatInput>(&message)) {
        handleChat(client, chat->text.left(256));
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
    client.entityId = m_nextEntityId++;
    client.name = hello.playerName.left(32);
    if (m_storage) {
        QStringList warnings;
        const QcResult<std::optional<QcPlayerData>> stored = m_storage->readPlayer(client.name, warnings);
        for (const QString &warning : std::as_const(warnings)) {
            qCWarning(lcServer) << warning;
        }
        if (!stored) {
            client.connection->send(QcMsg::Disconnect{u"Cannot load player data: %1"_s.arg(stored.error())});
            client.connection->close();
            return;
        }
        if (*stored) {
            client.motion = (*stored)->motion;
            client.yaw = (*stored)->yaw;
            client.pitch = (*stored)->pitch;
            client.inventory = (*stored)->inventory;
            client.state = Client::State::Playing;
            client.joined = true;
            client.stateDirty = true;
            qCInfo(lcServer) << "Player" << client.name << "rejoined as entity" << client.entityId;
            client.connection->send(QcMsg::Welcome{QcProtocol::version, client.entityId, m_config.worldName,
                                                   m_config.seed, client.yaw, client.pitch, true});
            client.connection->send(QcMsg::ContentSync{m_content});
            sendInventory(client);
            m_world.loadChunk(chunkOf(client.motion.position));
            streamChunks(client);
            return;
        }
    }
    client.state = Client::State::Playing;
    client.joined = true;
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

    client.connection->send(QcMsg::Welcome{QcProtocol::version, client.entityId, m_config.worldName, m_config.seed, 0.0f, 0.0f, false});
    client.connection->send(QcMsg::ContentSync{m_content});
    sendInventory(client);
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
    client.yaw = input.yaw;
    client.pitch = input.pitch;
    client.lastInputSequence = input.sequence;
    client.stateDirty = true;
}

const QcItemDefinition *QcServer::heldItem(const Client &client) const
{
    const QcItemStack &held = client.inventory.selectedStack();
    return !held.isEmpty() && m_content->isValidItem(held.item) ? &m_content->item(held.item) : nullptr;
}

bool QcServer::isWithinReach(const Client &client, QcBlockPos pos) const
{
    // One metre of slack for movement between the client's decision and the server's check.
    constexpr double tolerance = 1.0;
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    return (centre - eye).length() <= QcRaycast::playerReach + tolerance;
}

void QcServer::setBlockAndBroadcast(QcBlockPos pos, QcBlockId block)
{
    if (!m_world.setBlock(pos, block)) {
        return;
    }
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Playing && client->sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, block});
        }
    }
}

void QcServer::sendInventory(Client &client)
{
    client.connection->send(QcMsg::InventoryState{client.inventory.allSlots(), client.inventory.selectedHotbarSlot()});
}

void QcServer::handleFinishMining(Client &client, QcBlockPos pos)
{
    const bool sameTarget = client.miningPos && *client.miningPos == pos;
    client.miningPos.reset();
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    if (!sameTarget || !block || *block == qcAirBlockId || !isWithinReach(client, pos)) {
        // Resend the true block so a client that predicted the break shows it again.
        if (block) {
            client.connection->send(QcMsg::BlockUpdate{pos, *block});
        }
        return;
    }
    const QcBlockDefinition &definition = m_content->block(*block);
    const QcItemDefinition *held = heldItem(client);
    const double seconds = QcMining::breakSeconds(definition, held);
    const auto requiredTicks = quint64(std::ceil(seconds * QcPhysics::tickRate));
    if (seconds < 0.0 || m_tickCount - client.miningStartTick + miningToleranceTicks < requiredTicks) {
        client.connection->send(QcMsg::BlockUpdate{pos, *block});
        return;
    }
    setBlockAndBroadcast(pos, qcAirBlockId);
    // Drops go straight into the inventory; what does not fit is lost.
    if (QcMining::canHarvest(definition, held)) {
        for (const QcItemDrop &drop : definition.drops) {
            if (const std::optional<QcItemId> item = m_content->itemId(drop.item)) {
                client.inventory.add(*item, drop.count, *m_content);
            }
        }
    }
    if (held && held->durability > 0) {
        const int slot = client.inventory.selectedHotbarSlot();
        QcItemStack tool = client.inventory.slot(slot);
        if (++tool.damage >= held->durability) {
            tool = {};
        }
        client.inventory.setSlot(slot, tool);
    }
    sendInventory(client);
}

void QcServer::handleUseItem(Client &client, QcMsg::UseItem message)
{
    const QcBlockPos target = qcNeighbour(message.target, message.face);
    const QcItemDefinition *held = heldItem(client);
    const std::optional<QcBlockId> clicked = m_world.blockAt(message.target);
    const std::optional<QcBlockId> existing = m_world.blockAt(target);
    const std::optional<QcBlockId> placed = held ? m_content->blockId(held->placesBlock) : std::nullopt;
    const bool clickedSolid = clicked && m_content->isValidBlock(*clicked) && !m_content->block(*clicked).liquid;
    const auto reject = [&] {
        if (existing) {
            client.connection->send(QcMsg::BlockUpdate{target, *existing});
        }
        sendInventory(client);
    };
    if (!placed || !clickedSolid || *clicked == qcAirBlockId || !existing || !target.isInsideWorldHeight()
        || !m_content->block(*existing).replaceable || !isWithinReach(client, target)) {
        reject();
        return;
    }
    if (m_content->block(*placed).solid) {
        const QcAabb blockBox{{double(target.x), double(target.y), double(target.z)},
                              {target.x + 1.0, target.y + 1.0, target.z + 1.0}};
        for (const auto &other : m_clients) {
            if (other->state == Client::State::Playing
                && QcPhysics::boxAt(other->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight).intersects(blockBox)) {
                reject();
                return;
            }
        }
    }
    setBlockAndBroadcast(target, *placed);
    client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    sendInventory(client);
}

void QcServer::handleChat(Client &client, const QString &text)
{
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return;
    }
    const auto reply = [&client](const QString &line) { client.connection->send(QcMsg::ChatMessage{line}); };
    if (!words.first().startsWith(u'/')) {
        for (const auto &other : m_clients) {
            if (other->state == Client::State::Playing) {
                other->connection->send(QcMsg::ChatMessage{u"<%1> %2"_s.arg(client.name, text)});
            }
        }
        return;
    }
    const QString command = words.first().mid(1).toLower();
    if (command == u"help"_s) {
        reply(u"Commands: /help, /give <item> [count], /save"_s);
    } else if (command == u"save"_s) {
        const QcStatus saved = save();
        reply(saved ? u"World saved"_s : u"Saving failed: %1"_s.arg(saved.error()));
    } else if (command == u"give"_s && !m_config.allowCheats) {
        reply(u"Cheats are disabled on this server"_s);
    } else if (command == u"give"_s && words.size() >= 2) {
        const QString name = words[1].contains(u':') ? words[1] : u"qtcraft:"_s + words[1];
        const std::optional<QcItemId> item = m_content->itemId(name);
        bool countOk = true;
        const int count = words.size() >= 3 ? words[2].toInt(&countOk) : 1;
        if (!item || !countOk || count < 1 || count > 64 * QcInventory::slotCount) {
            reply(u"Unknown item or invalid count: %1"_s.arg(text));
            return;
        }
        const int leftover = client.inventory.add(*item, count, *m_content);
        sendInventory(client);
        reply(u"Gave %1 x %2"_s.arg(count - leftover).arg(name));
    } else {
        reply(u"Unknown command: %1"_s.arg(words.first()));
    }
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
    m_info.worldTime += QcPhysics::tickDuration;
    removeClosedClients();
    if (m_storage && m_config.autosaveSeconds > 0
        && m_tickCount % quint64(m_config.autosaveSeconds * QcPhysics::tickRate) == 0) {
        if (const QcStatus saved = save(); !saved) {
            qCWarning(lcServer) << "Autosave failed:" << saved.error();
            for (const auto &client : m_clients) {
                client->connection->send(QcMsg::ChatMessage{u"Autosave failed: %1"_s.arg(saved.error())});
            }
        }
    }
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
    for (const auto &client : m_clients) {
        if (client->state == Client::State::Closed && client->joined) {
            savePlayer(*client);
        }
    }
    const auto removed = std::erase_if(m_clients, [](const std::unique_ptr<Client> &client) {
        return client->state == Client::State::Closed;
    });
    if (removed > 0) {
        emit clientCountChanged(clientCount());
    }
}
