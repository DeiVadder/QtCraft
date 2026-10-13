#include "QcServerPersistence.h"

#include "QcBlockUpdateQueue.h"
#include "QcContentPackLoader.h"
#include "QcDayCycle.h"
#include "QcServerBlocks.h"
#include "QcServerGameModes.h"
#include "QcServerState.h"
#include "QcSignalStorage.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcPersistence, "qtcraft.server")

} // namespace

QcServerPersistence::QcServerPersistence(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates)
    : m_state(state)
    , m_blocks(blocks)
    , m_updates(updates)
{
}

QcServerPersistence::~QcServerPersistence() = default;

QcStatus QcServerPersistence::open()
{
    // New worlds, saved or not, start in the morning; their generator's setup is still to run.
    m_state.info.worldTime = QcDayCycle::startTime;
    m_state.info.keepInventory = m_state.config.keepInventory;
    m_state.info.defaultGameMode = m_state.config.defaultGameMode.value_or(QcGameMode::Survival);
    m_state.info.generator = m_state.config.generator;
    m_state.info.generatorOptions = m_state.config.generatorOptions;
    m_state.info.setupDone = false;

    if (m_state.config.worldDirectory.isEmpty()) {
        return {};
    }

    if (const QcStatus locked = lockWorld(); !locked) {
        return locked;
    }

    const QcWorldStorage &storage = m_storage.emplace(m_state.config.worldDirectory, m_state.content);
    if (!QFile::exists(QDir(m_state.config.worldDirectory).filePath(u"world.json"_s))) {
        return createInfo(storage);
    }

    QStringList warnings;
    if (const QcStatus read = readInfo(storage, warnings); !read) {
        return read;
    }

    const QcResult<qsizetype> chunkCount = readChunks(storage, warnings);
    if (!chunkCount) {
        return qcError(chunkCount.error());
    }

    m_setupSaved = m_state.info.setupDone;
    if (const QcStatus read = readWorldState(storage); !read) {
        return read;
    }

    qCInfo(lcPersistence) << "Opened world" << m_state.info.name << "with" << *chunkCount << "stored chunks and"
                          << m_state.items.items().size() << "dropped items";
    return {};
}

QcStatus QcServerPersistence::readWorldState(const QcWorldStorage &storage)
{
    // world.json is written last and says the setup is done only after everything else was saved.
    // Without that, what is stored may come from a save that broke off after a setup that brings
    // creatures, contents or plants: it is left out, the setup runs again and nothing exists twice.
    // Setups of only rules (terrain, flat, void) keep what is stored.
    const QcWorldSetup setup = m_state.world.generator().setup();
    const bool bringsState = !setup.mobs.isEmpty() || !setup.stands.isEmpty() || !setup.contents.isEmpty() || !setup.growing.isEmpty() || !setup.updates.isEmpty();
    if (!m_state.info.setupDone && bringsState) {
        qCInfo(lcPersistence) << "The world's setup was not saved completely; it runs again";
        return {};
    }

    if (const QcStatus read = readEntities(storage); !read) {
        return read;
    }
    if (const QcStatus read = readMobs(storage); !read) {
        return read;
    }
    if (const QcStatus read = readMachines(storage); !read) {
        return read;
    }
    if (const QcStatus read = readContainers(storage); !read) {
        return read;
    }
    readStands(storage);
    // Damaged circuits do not keep the world from opening: they start quiet (decision E-335).
    if (const QcStatus read = QcSignalStorage::read(m_state.config.worldDirectory, m_state.circuits, m_state.tickCount); !read) {
        noteWarnings({read.error() + u"; the circuits start quiet"_s});
    }
    return readBlockUpdates(storage);
}

QcStatus QcServerPersistence::createInfo(const QcWorldStorage &storage)
{
    m_state.info.name = m_state.config.worldName;
    m_state.info.seed = m_state.config.seed;
    m_state.info.created = QDateTime::currentDateTimeUtc();
    m_state.info.lastPlayed = m_state.info.created;
    m_state.info.packs = m_state.content->packs();
    return storage.writeInfo(m_state.info);
}

QcStatus QcServerPersistence::readInfo(const QcWorldStorage &storage, QStringList &warnings)
{
    QcResult<QcWorldInfo> info = storage.readInfo();
    if (!info) {
        return qcError(info.error());
    }

    if (info->seed != m_state.config.seed) {
        return qcError(u"World seed %1 does not match the generator seed %2"_s.arg(info->seed).arg(m_state.config.seed));
    }
    if (info->generator != m_state.config.generator) {
        return qcError(u"The world was made by the generator '%1', not '%2'"_s.arg(info->generator, m_state.config.generator));
    }
    if (info->generatorOptions != m_state.config.generatorOptions) {
        return qcError(u"The world was made with the options '%1', not '%2'"_s.arg(info->generatorOptions, m_state.config.generatorOptions));
    }

    m_state.info = *info;
    m_state.config.worldName = m_state.info.name;
    if (!m_state.info.unknownGameMode.isEmpty()) {
        warnings.append(u"The world names the unknown game mode '%1' for new players; they start in survival"_s.arg(m_state.info.unknownGameMode));
    }

    // Content of packs that are gone turns into air or disappears when chunks and players load.
    for (const QcPackReference &pack : std::as_const(m_state.info.packs)) {
        const auto current = std::ranges::find(m_state.content->packs(), pack.id, &QcPackReference::id);
        if (current == m_state.content->packs().end()) {
            warnings.append(u"The world was saved with '%1' %2, which is not loaded now"_s.arg(pack.name, pack.version));
        } else if (QcContentPackLoader::parseVersion(current->version) != QcContentPackLoader::parseVersion(pack.version)) {
            warnings.append(u"The world was saved with '%1' %2, now %3 is loaded"_s.arg(pack.name, pack.version, current->version));
        }
    }

    m_state.info.packs = m_state.content->packs();
    return {};
}

QcResult<qsizetype> QcServerPersistence::readChunks(const QcWorldStorage &storage, QStringList &warnings)
{
    QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> chunks = storage.readChunks(warnings);
    m_openWarnings.clear();
    noteWarnings(warnings);
    if (!chunks) {
        return qcError(chunks.error());
    }

    m_state.world.adoptStoredChunks(*chunks);
    return chunks->size();
}

QcStatus QcServerPersistence::readEntities(const QcWorldStorage &storage)
{
    QStringList warnings;
    const QcResult<QcWorldEntities> entities = storage.readEntities(warnings);
    noteWarnings(warnings);
    if (!entities) {
        return qcError(entities.error());
    }

    m_state.items.restore(entities->items, [this] { return m_state.newEntityId(); });
    m_state.plants.restoreSaplings(entities->saplings);
    m_state.falling.restore(entities->fallingBlocks, [this] { return m_state.newEntityId(); });
    return {};
}

QcStatus QcServerPersistence::readMobs(const QcWorldStorage &storage)
{
    QStringList warnings;
    const QcResult<QcWorldMobs> mobs = storage.readMobs(warnings);
    noteWarnings(warnings);
    if (!mobs) {
        return qcError(mobs.error());
    }
    m_state.mobs.restore(mobs->mobs, mobs->populatedChunks, [this] { return m_state.newEntityId(); });
    return {};
}

QcStatus QcServerPersistence::readMachines(const QcWorldStorage &storage)
{
    QStringList warnings;
    const QcResult<QList<QcMachineManager::Machine>> machines = storage.readMachines(warnings);
    noteWarnings(warnings);
    if (!machines) {
        return qcError(machines.error());
    }
    m_state.machines.restore(*machines);
    return {};
}

QcStatus QcServerPersistence::readContainers(const QcWorldStorage &storage)
{
    QStringList warnings;
    const QcResult<QList<QcContainerManager::Container>> containers = storage.readContainers(warnings);
    noteWarnings(warnings);
    if (!containers) {
        return qcError(containers.error());
    }

    m_state.containers.restore(*containers);

    // Chests whose block went away with a mod (or whose size changed) must not linger.
    const QList<QcBlockPos> chestPositions = m_state.containers.positions();
    for (const QcBlockPos &pos : chestPositions) {
        m_state.world.loadChunk(pos.chunk());
        m_blocks.reconcileContainer(pos);
    }
    return {};
}

void QcServerPersistence::readStands(const QcWorldStorage &storage)
{
    QStringList warnings;
    QList<QcArmourStandManager::Stand> stands = storage.readStands(warnings);
    for (QcArmourStandManager::Stand &stand : stands) {
        stand.id = m_state.newEntityId();
    }

    if (const qsizetype dropped = m_state.stands.restore(std::move(stands)); dropped > 0) {
        warnings.append(u"%1 armour stands beyond the limit of %2 were left out"_s.arg(dropped).arg(QcArmourStandManager::maxStands));
    }
    noteWarnings(warnings);
}

QcStatus QcServerPersistence::readBlockUpdates(const QcWorldStorage &storage)
{
    const QcResult<QList<QcBlockPos>> updates = storage.readBlockUpdates();
    if (!updates) {
        return qcError(updates.error());
    }

    m_updates.setCommandMode(true);
    for (const QcBlockPos &pos : *updates) {
        m_updates.queue(pos);
    }
    m_updates.setCommandMode(false);
    return {};
}

void QcServerPersistence::noteWarnings(const QStringList &warnings)
{
    for (const QString &warning : warnings) {
        qCWarning(lcPersistence) << warning;
    }
    m_openWarnings.append(warnings);
}

QcStatus QcServerPersistence::lockWorld()
{
    const QDir directory(m_state.config.worldDirectory);
    if (!QDir().mkpath(directory.path())) {
        return qcError(u"Cannot create %1"_s.arg(directory.path()));
    }

    // A lock never goes stale by age, only when its process is gone (QLockFile checks the PID).
    auto lock = std::make_unique<QLockFile>(directory.filePath(lockFileName.toString()));
    lock->setStaleLockTime(0);
    if (lock->tryLock(0)) {
        m_lock = std::move(lock);
        return {};
    }

    if (lock->error() == QLockFile::LockFailedError) {
        m_worldInUse = true;
        return qcError(u"The world %1 is already open in another QtCraft window or process"_s.arg(directory.path()));
    }
    return qcError(u"Cannot lock the world %1"_s.arg(directory.path()));
}

QcResult<std::optional<QcPlayerData>> QcServerPersistence::readPlayer(const QString &name, QStringList &warnings) const
{
    return m_storage ? m_storage->readPlayer(name, warnings) : QcResult<std::optional<QcPlayerData>>(std::nullopt);
}

QcStatus QcServerPersistence::savePlayer(const QcServerClient &client)
{
    // Only joined players are saved, and their names are never empty (QcProfileValidation).
    Q_ASSERT(client.joined && !client.name.isEmpty());
    if (!m_storage) {
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
            lost += player.inventory.add(cell.item, cell.count, *m_state.content, cell.damage);
        }
    }

    if (lost > 0) {
        qCWarning(lcPersistence) << "Inventory of" << client.name << "is full:" << lost << "items from the crafting grid were not saved";
    }

    player.health = client.health;
    player.food = client.hunger.food();
    player.exhaustion = client.hunger.exhaustion();
    player.equipment = client.equipment;
    player.gameMode = client.gameMode;
    player.keptInventory = client.keptInventory;
    player.keptEquipment = client.keptEquipment;

    const QcStatus saved = m_storage->writePlayer(client.name, player);
    if (!saved) {
        qCWarning(lcPersistence) << saved.error();
    }
    return saved;
}

void QcServerPersistence::restorePlayer(QcServerClient &client, const QcPlayerData &player)
{
    client.motion = player.motion;
    client.fallPeak = client.motion.position.y;
    client.yaw = player.yaw;
    client.pitch = player.pitch;
    client.inventory = player.inventory;
    client.equipment = player.equipment;
    client.health = std::clamp(player.health, 0.0, QcServerClient::maxHealth);
    client.hunger.restore(player.food, player.exhaustion);
    client.gameMode = player.gameMode;
    client.keptInventory = player.keptInventory;
    client.keptEquipment = player.keptEquipment;
    QcServerGameModes::applyRules(client);
}

QcStatus QcServerPersistence::save()
{
    if (!m_storage) {
        return {};
    }

    const QcWorldStorage &storage = *m_storage;
    QcStatus result;
    const auto keep = [&result](const QcStatus &written) {
        if (!written && result) {
            result = written;
        }
    };

    // Players first: their inventories matter most and must not depend on the world files.
    for (const auto &client : m_state.clients) {
        if (client->joined) {
            keep(savePlayer(*client));
        }
    }

    const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks = m_state.world.takeChunksToSave();
    if (const QcStatus written = storage.writeChunks(chunks); !written) {
        // Keep the regions dirty so the next save tries again.
        m_state.world.markDirty(chunks.keys());
        keep(written);
    }

    keep(saveEntities(storage));
    keep(storage.writeMobs({m_state.mobs.persistentMobs(), m_state.mobs.populatedChunks()}));
    keep(storage.writeMachines(m_state.machines.machines()));
    keep(storage.writeContainers(m_state.containers.containers()));
    keep(storage.writeStands(m_state.stands.stands()));
    keep(saveBlockUpdates(storage));
    keep(QcSignalStorage::write(m_state.config.worldDirectory, m_state.circuits, m_state.tickCount));

    // Last: the setup counts as done once everything it brought is stored (see readWorldState()).
    m_state.info.lastPlayed = QDateTime::currentDateTimeUtc();
    QcWorldInfo info = m_state.info;
    info.setupDone = m_state.info.setupDone && (m_setupSaved || result.has_value());
    const QcStatus written = storage.writeInfo(info);
    m_setupSaved = m_setupSaved || (written && info.setupDone);
    keep(written);
    return result;
}

QcStatus QcServerPersistence::saveEntities(const QcWorldStorage &storage) const
{
    QcWorldEntities entities;
    for (const QcItemEntityManager::Item &item : m_state.items.items()) {
        entities.items.append({item.stack, item.motion.position, item.motion.velocity, item.pickupDelay, item.age});
    }

    // Waiting drops follow the items; on the next start they wait again if there is no room.
    entities.items.append(m_state.items.queued());
    entities.saplings = m_state.plants.saplings();
    entities.fallingBlocks = m_state.falling.all();
    return storage.writeEntities(entities);
}

QcStatus QcServerPersistence::saveBlockUpdates(const QcWorldStorage &storage) const
{
    // Pending neighbour updates survive a restart, so a large /fill never leaves floating torches.
    // Those of player actions come first when there are more than a save holds.
    QList<QcBlockPos> updates = m_updates.playerUpdates();
    // Planned flow steps come back as updates, which plan them again.
    updates.append(m_state.liquids.plannedPositions());
    updates.append(m_updates.commandUpdates());
    updates.append(m_updates.waitingUpdates());

    const qsizetype storable = std::min(m_state.config.maxSavedBlockUpdates, qsizetype(QcWorldStorage::maxStoredBlockUpdates));
    if (updates.size() > storable) {
        qCWarning(lcPersistence) << "Saving only" << storable << "of" << updates.size() << "pending block updates";
        updates.resize(storable);
    }

    return storage.writeBlockUpdates(updates);
}
