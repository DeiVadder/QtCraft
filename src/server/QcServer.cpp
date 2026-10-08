#include "QcServer.h"

#include "QcConnection.h"
#include "QcContentPackLoader.h"
#include "QcDayCycle.h"
#include "QcInventory.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcRaycast.h"
#include "QcServerClient.h"
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

constexpr int tickIntervalMs = int(QcPhysics::tickDuration * 1000.0);
// Bounded so that turning around re-prioritises quickly instead of finishing a long old queue.
constexpr qsizetype maxPendingGeneration = 48;

QcChunkPos chunkOf(const QcVec3 &position)
{
    return QcBlockPos{int(std::floor(position.x)), 0, int(std::floor(position.z))}.chunk();
}

} // namespace


QcServer::QcServer(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                   std::unique_ptr<QcWorldGenerator> generator, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_content(content)
    , m_world(std::move(content), std::move(generator))
    , m_tickTimer(new QTimer(this))
    , m_mobs(m_content, quint64(m_config.seed))
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
        m_info.worldTime = QcDayCycle::startTime;
        m_info.name = m_config.worldName;
        m_info.seed = m_config.seed;
        m_info.created = QDateTime::currentDateTimeUtc();
        m_info.lastPlayed = m_info.created;
        m_info.packs = m_content->packs();
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
    // Content of packs that are gone turns into air or disappears when chunks and players load.
    for (const QcPackReference &pack : std::as_const(m_info.packs)) {
        const auto current = std::ranges::find(m_content->packs(), pack.id, &QcPackReference::id);
        if (current == m_content->packs().end()) {
            warnings.append(u"The world was saved with '%1' %2, which is not loaded now"_s.arg(pack.name, pack.version));
        } else if (QcContentPackLoader::parseVersion(current->version) != QcContentPackLoader::parseVersion(pack.version)) {
            warnings.append(u"The world was saved with '%1' %2, now %3 is loaded"_s.arg(pack.name, pack.version, current->version));
        }
    }
    m_info.packs = m_content->packs();
    QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> chunks = m_storage->readChunks(warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcServer) << warning;
    }
    m_openWarnings = warnings;
    if (!chunks) {
        return qcError(chunks.error());
    }
    m_world.adoptStoredChunks(*chunks);
    qCInfo(lcServer) << "Opened world" << m_info.name << "with" << chunks->size() << "stored chunks";
    return {};
}

QcStatus QcServer::savePlayer(const Client &client)
{
    if (!m_storage || client.name.isEmpty()) {
        return {};
    }
    QcPlayerData player;
    player.motion = client.motion;
    player.yaw = client.yaw;
    player.pitch = client.pitch;
    player.inventory = client.inventory;
    // Items in the crafting grid are stored in the inventory, as if the grid had been closed; the
    // open grid itself is not saved.
    int lost = 0;
    for (const QcItemStack &cell : client.craftingGrid) {
        if (!cell.isEmpty()) {
            lost += player.inventory.add(cell.item, cell.count, *m_content);
        }
    }
    if (lost > 0) {
        qCWarning(lcServer) << "Inventory of" << client.name << "is full:" << lost << "items from the crafting grid were not saved";
    }
    player.health = client.health;
    const QcStatus saved = m_storage->writePlayer(client.name, player);
    if (!saved) {
        qCWarning(lcServer) << saved.error();
    }
    return saved;
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
            if (const QcStatus saved = savePlayer(*client); !saved) {
                return saved;
            }
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
    // The dead neither mine nor use items; their mining state is dropped.
    const bool worldAction = std::holds_alternative<QcMsg::StartMining>(message)
                             || std::holds_alternative<QcMsg::FinishMining>(message)
                             || std::holds_alternative<QcMsg::UseItem>(message);
    if (client.dead && worldAction) {
        client.miningPos.reset();
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
        client.miningPos.reset();
        handleMoveItem(client, *move);
    } else if (std::holds_alternative<QcMsg::TakeCraftingResult>(message)) {
        handleTakeCraftingResult(client);
    } else if (const auto *craft = std::get_if<QcMsg::CraftRecipe>(&message)) {
        handleCraftRecipe(client, *craft);
    } else if (std::holds_alternative<QcMsg::CloseCrafting>(message)) {
        closeCrafting(client);
    } else if (std::holds_alternative<QcMsg::ConsumeItem>(message)) {
        handleConsume(client);
    } else if (const auto *attack = std::get_if<QcMsg::AttackEntity>(&message)) {
        handleAttack(client, attack->id);
    } else if (std::holds_alternative<QcMsg::Respawn>(message)) {
        handleRespawn(client);
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
            client.fallPeak = client.motion.position.y;
            client.yaw = (*stored)->yaw;
            client.pitch = (*stored)->pitch;
            client.inventory = (*stored)->inventory;
            client.health = std::clamp((*stored)->health, 0.0, playerMaxHealth);
            // Who left while dead comes back to the death screen and respawns from there.
            client.dead = client.health <= 0.0;
            client.state = Client::State::Playing;
            client.joined = true;
            client.stateDirty = true;
            qCInfo(lcServer) << "Player" << client.name << "rejoined as entity" << client.entityId;
            client.connection->send(QcMsg::Welcome{QcProtocol::version, client.entityId, m_config.worldName,
                                                   m_config.seed, client.yaw, client.pitch, true});
            client.connection->send(QcMsg::ContentSync{m_content});
            sendInventory(client);
            sendHealth(client);
            sendTime(client);
            sendOpenWarnings(client);
            m_world.loadChunk(chunkOf(client.motion.position));
            streamChunks(client);
            return;
        }
    }
    client.state = Client::State::Playing;
    client.joined = true;
    client.motion.position = spawnPosition();
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
    qCInfo(lcServer) << "Player" << client.name << "joined as entity" << client.entityId;

    client.connection->send(QcMsg::Welcome{QcProtocol::version, client.entityId, m_config.worldName, m_config.seed, 0.0f, 0.0f, false});
    client.connection->send(QcMsg::ContentSync{m_content});
    sendInventory(client);
    sendHealth(client);
    sendTime(client);
    sendOpenWarnings(client);
    streamChunks(client);
}

void QcServer::sendOpenWarnings(Client &client)
{
    // The player should know why blocks or items of a removed mod are gone.
    constexpr qsizetype shown = 5;
    const QStringList first = m_openWarnings.first(std::min(shown, m_openWarnings.size()));
    for (const QString &warning : first) {
        client.connection->send(QcMsg::ChatMessage{warning});
    }
    if (m_openWarnings.size() > shown) {
        client.connection->send(QcMsg::ChatMessage{u"... and %1 more warnings in the log"_s.arg(m_openWarnings.size() - shown)});
    }
}

QcVec3 QcServer::spawnPosition()
{
    QcBlockPos spawn = m_world.generator().spawnPoint();
    if (m_config.spawnColumn) {
        spawn = {(*m_config.spawnColumn)[0], 0, (*m_config.spawnColumn)[1]};
    }
    // Stand on whatever the generated column really contains (caves, trees, water surface).
    const std::shared_ptr<const QcChunk> spawnChunk = m_world.loadChunk(spawn.chunk());
    spawn.y = spawnChunk->highestNonAir(spawn.localX(), spawn.localZ()) + 1;
    return {spawn.x + 0.5, double(spawn.y), spawn.z + 0.5};
}

void QcServer::handleInput(Client &client, const QcMsg::PlayerInput &message)
{
    if (client.inputBudget <= 0) {
        return;
    }
    if (client.dead) {
        // Acknowledge without moving so the client's queue of predicted inputs drains.
        client.lastInputSequence = message.input.sequence;
        client.stateDirty = true;
        return;
    }
    --client.inputBudget;
    // The decoder already rejects non-finite values; ranges are enforced here.
    QcPlayerInput input = message.input;
    input.forward = std::clamp(input.forward, -1.0f, 1.0f);
    input.strafe = std::clamp(input.strafe, -1.0f, 1.0f);
    input.pitch = std::clamp(input.pitch, -90.0f, 90.0f);
    input.yaw = std::fmod(input.yaw, 360.0f);
    const bool wasOnGround = client.motion.onGround;
    const bool wasInWater = client.motion.inWater;
    const double previousY = client.motion.position.y;
    QcPhysics::stepPlayer(client.motion, input, m_world, QcPhysics::tickDuration);
    updateHealth(client, wasOnGround, wasInWater, previousY);
    client.yaw = input.yaw;
    client.pitch = input.pitch;
    client.sneaking = input.sneak;
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
    tickGameplay();
    for (const auto &client : m_clients) {
        if (client->state != Client::State::Playing) {
            continue;
        }
        client->inputBudget = std::min(client->inputBudget + 1, maxInputBudget);
        streamChunks(*client);
        syncEntities(*client);
        if (m_tickCount % quint64(QcPhysics::tickRate) == 0) {
            sendTime(*client);
        }
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
            // Failures are logged by savePlayer; the client is gone and cannot be told.
            [[maybe_unused]] const QcStatus saved = savePlayer(*client);
        }
    }
    const auto removed = std::erase_if(m_clients, [](const std::unique_ptr<Client> &client) {
        return client->state == Client::State::Closed;
    });
    if (removed > 0) {
        emit clientCountChanged(clientCount());
    }
}
