#include "QcServerPersistence.h"

#include "QcBlockUpdateQueue.h"
#include "QcContentPackLoader.h"
#include "QcDayCycle.h"
#include "QcServerBlocks.h"
#include "QcPlayerRecords.h"
#include "QcServerState.h"
#include "QcBlockEntityRecords.h"
#include "QcEntityRecords.h"

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
    m_state.info().worldTime = QcDayCycle::startTime;
    m_state.info().keepInventory = m_state.config().keepInventory;
    m_state.info().defaultGameMode = m_state.config().defaultGameMode.value_or(QcGameMode::Survival);
    m_state.info().generator = m_state.config().generator;
    m_state.info().generatorOptions = m_state.config().generatorOptions;
    m_state.info().setupDone = false;

    if (m_state.config().worldDirectory.isEmpty()) {
        return {};
    }

    // A world of an older development version is refused before anything of it is read or written,
    // the lock file included (decision E-356): its format is not converted.
    const int version = QcWorldStorage(m_state.config().worldDirectory, m_state.contentPointer()).storedFormatVersion();
    if (version > 0 && version < QcWorldInfo::oldestFormatVersion) {
        m_worldTooOld = true;
        return qcError(u"The world was saved by an older development version of QtCraft (format %1) and cannot be opened"_s.arg(version));
    }

    if (const QcStatus locked = lockWorld(); !locked) {
        return locked;
    }

    const QcWorldStorage &storage = m_storage.emplace(m_state.config().worldDirectory, m_state.contentPointer());
    if (!QFile::exists(QDir(m_state.config().worldDirectory).filePath(u"world.json"_s))) {
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

    m_setupSaved = m_state.info().setupDone;
    if (const QcStatus read = readWorldState(storage); !read) {
        return read;
    }

    qCInfo(lcPersistence) << "Opened world" << m_state.info().name << "with" << *chunkCount << "stored chunks and"
                          << m_state.entities().items().items().size() << "dropped items";
    return {};
}

QcStatus QcServerPersistence::readWorldState(const QcWorldStorage &storage)
{
    // world.json is written last and says the setup is done only after everything else was saved.
    // Without that, what is stored may come from a save that broke off after a setup that brings
    // creatures, contents or plants: it is left out, the setup runs again and nothing exists twice.
    // Setups of only rules (terrain, flat, void) keep what is stored.
    const QcWorldSetup setup = m_state.world().generator().setup();
    const bool bringsState = !setup.mobs.isEmpty() || !setup.stands.isEmpty() || !setup.contents.isEmpty() || !setup.growing.isEmpty() || !setup.updates.isEmpty();
    if (!m_state.info().setupDone && bringsState) {
        qCInfo(lcPersistence) << "The world's setup was not saved completely; it runs again";
        return {};
    }

    if (const QcStatus read = readEntities(storage); !read) {
        return read;
    }
    if (const QcStatus read = readBlockEntities(storage); !read) {
        return read;
    }
    return readBlockUpdates(storage);
}

QcStatus QcServerPersistence::createInfo(const QcWorldStorage &storage)
{
    m_state.info().name = m_state.config().worldName;
    m_state.info().seed = m_state.config().seed;
    m_state.info().created = QDateTime::currentDateTimeUtc();
    m_state.info().lastPlayed = m_state.info().created;
    m_state.info().packs = m_state.content().packs();
    return storage.writeInfo(m_state.info());
}

QcStatus QcServerPersistence::readInfo(const QcWorldStorage &storage, QStringList &warnings)
{
    QcResult<QcWorldInfo> info = storage.readInfo();
    if (!info) {
        return qcError(info.error());
    }

    if (info->seed != m_state.config().seed) {
        return qcError(u"World seed %1 does not match the generator seed %2"_s.arg(info->seed).arg(m_state.config().seed));
    }
    if (info->generator != m_state.config().generator) {
        return qcError(u"The world was made by the generator '%1', not '%2'"_s.arg(info->generator, m_state.config().generator));
    }
    if (info->generatorOptions != m_state.config().generatorOptions) {
        return qcError(u"The world was made with the options '%1', not '%2'"_s.arg(info->generatorOptions, m_state.config().generatorOptions));
    }

    m_state.info() = *info;
    m_state.config().worldName = m_state.info().name;
    // A single-player world plays with its own limits; a dedicated server's server.json decides there.
    if (!m_state.config().limitsFromServer) {
        m_state.config().limits = QcServerLimits::fromJson(m_state.info().limits, u"world.json"_s, &warnings);
        m_state.applyLimits();
    }
    if (!m_state.info().unknownGameMode.isEmpty()) {
        warnings.append(u"The world names the unknown game mode '%1' for new players; they start in survival"_s.arg(m_state.info().unknownGameMode));
    }

    // Content of packs that are gone turns into air or disappears when chunks and players load.
    for (const QcPackReference &pack : std::as_const(m_state.info().packs)) {
        const auto current = std::ranges::find(m_state.content().packs(), pack.id, &QcPackReference::id);
        if (current == m_state.content().packs().end()) {
            warnings.append(u"The world was saved with '%1' %2, which is not loaded now"_s.arg(pack.name, pack.version));
        } else if (QcContentPackLoader::parseVersion(current->version) != QcContentPackLoader::parseVersion(pack.version)) {
            warnings.append(u"The world was saved with '%1' %2, now %3 is loaded"_s.arg(pack.name, pack.version, current->version));
        }
    }

    m_state.info().packs = m_state.content().packs();
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

    m_state.world().adoptStoredChunks(*chunks);
    return chunks->size();
}

QcStatus QcServerPersistence::readEntities(const QcWorldStorage &storage)
{
    QStringList warnings;
    const QcResult<QcWorldEntityRecords> entities = storage.readEntities(warnings);
    noteWarnings(warnings);
    if (!entities) {
        m_damagedFile = {DamagedPart::Entities, storage.damagedEntityFile()};
        return qcError(u"%1; moving %2 out of the world folder opens the world without the creatures, items and armour stands "
                       u"saved in it"_s.arg(entities.error(), m_damagedFile.file));
    }
    noteWarnings(QcEntityRecords::apply(*entities, m_state.entities()));
    return {};
}

QcStatus QcServerPersistence::readBlockEntities(const QcWorldStorage &storage)
{
    QStringList warnings;
    const QcResult<QList<QcBlockEntityRecord>> records = storage.readBlockEntities(warnings);
    noteWarnings(warnings);
    if (!records) {
        m_damagedFile = {DamagedPart::BlockEntities, storage.damagedBlockEntityFile()};
        return qcError(u"%1; moving %2 out of the world folder opens the world without the contents of the chests and machines "
                       u"saved in it"_s.arg(records.error(), m_damagedFile.file));
    }
    noteWarnings(QcBlockEntityRecords::apply(*records, m_state.blockEntities(), m_state.circuits(), m_state.hoppers(), m_state.clock().tick()));

    // Chests whose block went away with a mod (or whose size changed) must not linger: their chunks
    // load now, everything kept for a loaded chunk is fitted to its blocks (other chunks are fitted
    // when they arrive) and spilled contents drop.
    for (const QcBlockEntityRecord &record : *records) {
        if (!record.containers.isEmpty()) {
            m_state.world().loadChunk(record.chunk);
        }
    }
    for (const QcBlockEntityRecord &record : *records) {
        if (const std::shared_ptr<const QcChunk> chunk = m_state.world().chunk(record.chunk)) {
            m_state.blockEntities().reconcile(*chunk);
        }
    }
    m_blocks.dropSpills();
    return {};
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
    const QDir directory(m_state.config().worldDirectory);
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

    int lost = 0;
    const QcPlayerData player = QcPlayerRecords::of(client, m_state.content(), lost);
    if (lost > 0) {
        qCWarning(lcPersistence) << "Inventory of" << client.name << "is full:" << lost << "items from the crafting grid were not saved";
    }

    const QcStatus saved = m_storage->writePlayer(client.name, player);
    if (!saved) {
        qCWarning(lcPersistence) << saved.error();
    }
    return saved;
}

QcStatus QcServerPersistence::save()
{
    if (!m_storage) {
        return {};
    }

    const QcWorldStorage &storage = *m_storage;
    // Contents spilled since the last tick lie on the ground before the items are written.
    m_blocks.dropSpills();
    QcStatus result;
    const auto keep = [&result](const QcStatus &written) {
        if (!written && result) {
            result = written;
        }
    };

    // Players first: their inventories matter most and must not depend on the world files.
    for (const auto &client : m_state.clients()) {
        if (client->joined) {
            keep(savePlayer(*client));
        }
    }

    const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks = m_state.world().takeChunksToSave();
    if (const QcStatus written = storage.writeChunks(chunks); !written) {
        // Keep the regions dirty so the next save tries again.
        m_state.world().markDirty(chunks.keys());
        keep(written);
    }

    keep(storage.writeEntities(QcEntityRecords::collect(m_state.entities())));
    keep(storage.writeBlockEntities(QcBlockEntityRecords::collect(m_state.blockEntities(), m_state.circuits(), m_state.clock().tick())));
    keep(saveBlockUpdates(storage));

    // Last: the setup counts as done once everything it brought is stored (see readWorldState()).
    m_state.info().lastPlayed = QDateTime::currentDateTimeUtc();
    QcWorldInfo info = m_state.info();
    info.setupDone = m_state.info().setupDone && (m_setupSaved || result.has_value());
    const QcStatus written = storage.writeInfo(info);
    m_setupSaved = m_setupSaved || (written && info.setupDone);
    keep(written);
    return result;
}

QcStatus QcServerPersistence::saveBlockUpdates(const QcWorldStorage &storage) const
{
    // Pending neighbour updates survive a restart, so a large /fill never leaves floating torches.
    // Those of player actions come first when there are more than a save holds.
    QList<QcBlockPos> updates = m_updates.playerUpdates();
    // Planned flow steps come back as updates, which plan them again.
    updates.append(m_state.liquids().plannedPositions());
    updates.append(m_updates.commandUpdates());
    updates.append(m_updates.waitingUpdates());

    const qsizetype storable = std::min(qsizetype(m_state.config().limits.savedBlockUpdates), qsizetype(QcWorldStorage::maxStoredBlockUpdates));
    if (updates.size() > storable) {
        qCWarning(lcPersistence) << "Saving only" << storable << "of" << updates.size() << "pending block updates";
        updates.resize(storable);
    }

    return storage.writeBlockUpdates(updates);
}
