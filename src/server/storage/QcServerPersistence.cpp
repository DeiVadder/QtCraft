#include "server/storage/QcServerPersistence.h"

#include "common/content/loader/QcContentPackLoader.h"
#include "common/world/QcDayCycle.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/chunks/QcChunkLoading.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/maps/QcMapSaveCodec.h"
#include "server/minecarts/QcServerMinecarts.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcPlayerRecords.h"
#include "server/storage/QcSaveFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

#include <algorithm>

using namespace Qt::StringLiterals;

QcServerPersistence::QcServerPersistence(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates, QcChunkLoading &loading)
    : m_state(state)
    , m_blocks(blocks)
    , m_updates(updates)
    , m_loading(loading)
{
}

QcServerPersistence::~QcServerPersistence()
{
    // The world must not read or keep chunks through a storage that is gone.
    m_state.world().setStorage(nullptr, {});
}

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
        // Without a world directory unloaded chunks and maps stay in memory (QcChunkStore without
        // directory).
        m_state.maps().open({}, 1);
        return openChunks({});
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
    // Before world.json is read: a save that broke off after its journal is finished first.
    m_openWarnings.clear();
    if (const QcStatus opened = openChunks(m_state.config().worldDirectory); !opened) {
        return opened;
    }
    openMaps();
    if (!QFile::exists(QDir(m_state.config().worldDirectory).filePath(u"world.json"_s))) {
        return createInfo(storage);
    }

    const QcResult<QStringList> warnings = readInfo(storage);
    if (!warnings) {
        return qcError(warnings.error());
    }
    noteWarnings(*warnings);

    // A save writes world.json, the chunks and their entities as one (decision E-373): a world whose
    // setup is not done has nothing of it stored, so the setup simply runs.
    if (const QcStatus read = readBlockUpdates(storage); !read) {
        return read;
    }
    restoreVillages(storage);

    qCInfo(lcServer) << "Opened world" << m_state.info().name << "with" << m_state.world().saves().storedChunkCount() << "stored chunks";
    return {};
}

QcStatus QcServerPersistence::openChunks(const QString &directory)
{
    m_io = std::make_unique<QcChunkIo>(directory, m_state.contentPointer());
    QcResult<QHash<QcChunkPos, QcChunkStore::Stored>> catalog = m_io->open();
    if (!catalog) {
        const std::optional<QcChunkStore::Damaged> damaged = m_io->damaged();
        if (!damaged) {
            return qcError(catalog.error());
        }
        if (damaged->journal) {
            m_damagedFile = {DamagedPart::Journal, damaged->file};
            return qcError(u"%1; removing %2 and every file ending in .next from the world folder opens the world as it was saved before"_s.arg(
                catalog.error(), damaged->file));
        }
        using Kind = QcChunkStore::Kind;
        const DamagedPart part = damaged->kind == Kind::Entities ? DamagedPart::Entities
                                 : damaged->kind == Kind::BlockEntities ? DamagedPart::BlockEntities
                                                                        : DamagedPart::Blocks;
        m_damagedFile = {part, damaged->file};
        const QString without = part == DamagedPart::Entities        ? u"the creatures, items and armour stands saved in it"_s
                                : part == DamagedPart::BlockEntities ? u"the contents of the chests and machines saved in it"_s
                                                                     : u"the changes of the chunks saved in it (they are generated anew)"_s;
        return qcError(u"%1; moving %2 out of the world folder opens the world without %3"_s.arg(catalog.error(), damaged->file, without));
    }
    m_state.world().setStorage(m_io.get(), std::move(*catalog));
    return {};
}

void QcServerPersistence::openMaps()
{
    const QDir folder(QDir(m_state.config().worldDirectory).filePath(u"maps"_s));
    quint32 next = 1;
    const QStringList files = folder.entryList({u"*.qcmap"_s}, QDir::Files);
    for (const QString &name : files) {
        if (const std::optional<quint32> id = QcMapSaveCodec::idOf(name)) {
            next = std::max(next, *id + 1);
        } else {
            qCWarning(lcServer) << "Map file" << name << "has no valid map id; left alone";
        }
    }
    const std::shared_ptr<const QcContentRegistry> content = m_state.contentPointer();
    m_state.maps().open(
        [folder, content](quint32 id) -> std::optional<QcMapData> {
            QFile file(folder.filePath(QcMapSaveCodec::fileName(id).section(u'/', -1)));
            if (!file.exists()) {
                return std::nullopt;
            }
            const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.read(QcMapSaveCodec::maxBytes + 1) : QByteArray();
            QcResult<QcMapData> map = QcMapSaveCodec::read(bytes, file.fileName(), *content);
            if (!map) {
                // The id stays used; the damaged file is kept aside for a look.
                qCWarning(lcServer) << map.error() << "- kept aside as" << QcSaveFile::keepAside(file.fileName());
                return std::nullopt;
            }
            return std::move(*map);
        },
        next);
}

void QcServerPersistence::restoreVillages(const QcWorldStorage &storage)
{
    QcVillages &villages = m_state.world().villages();
    const QcVillages::Limits &limits = villages.limits();
    // A plan never leaves its region, and a village grows (decision E-490) at most to limits.extent.
    const int extent = std::max(m_state.content().villageCatalog().settings().spacing, limits.extent);
    const QcVillage::ReadLimits read{extent, limits.beds, limits.workstations};
    const QcResult<QList<QcVillage>> stored = storage.readVillages(read);
    if (stored) {
        villages.restore(*stored);
        m_villagesSaved = villages.changes();
        return;
    }

    // The villages follow from the generator's plans again as their chunks load; what only the file
    // knew (claims, population) starts over. The next save writes a new file.
    villages.restore({});
    m_villagesSaved.reset();
    const QString path = QDir(m_state.config().worldDirectory).filePath(QString::fromLatin1(QcWorldStorage::villagesFileName));
    const QString copy = QcSaveFile::keepAside(path);
    if (copy.isEmpty()) {
        noteWarnings({u"%1; the villages start anew from the world generator, and the file could not be kept aside: the next save replaces it"_s.arg(
            stored.error())});
        return;
    }
    QFile::remove(path);
    noteWarnings({u"%1; the villages start anew from the world generator, the damaged file was kept as %2"_s.arg(stored.error(),
                                                                                                               QFileInfo(copy).fileName())});
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

QcResult<QStringList> QcServerPersistence::readInfo(const QcWorldStorage &storage)
{
    QStringList warnings;
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
    m_state.world().loaders().restore(m_state.info().chunkLoaders);
    m_state.entities().creatures().setNextUid(m_state.info().nextCreatureId);
    // Ids of maps kept aside or lost stay used (decision E-799).
    m_state.maps().reserveIdsBelow(m_state.info().nextMapId);
    m_state.config().worldName = m_state.info().name;
    // A single-player world plays with its own limits; a dedicated server's server.json decides there.
    if (!m_state.config().limitsFromServer) {
        const QcServerLimits::Read limits = QcServerLimits::fromJson(m_state.info().limits, u"world.json"_s);
        m_state.config().limits = limits.limits;
        warnings.append(limits.warnings);
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
    return warnings;
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
        qCWarning(lcServer) << warning;
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

QcPlayerStorage::Result QcServerPersistence::readPlayer(const QString &name) const
{
    if (!m_storage || !m_io) {
        return QcPlayerStorage::Read{};
    }
    // Through the storage: a file handed over with a save that is not written yet is the newest.
    const QcResult<std::optional<QByteArray>> file = m_io->readFileNow(QcPlayerStorage::relativePath(name));
    if (!file) {
        return std::unexpected(QcPlayerStorage::Failure{file.error(), {}});
    }
    return m_storage->parsePlayer(name, *file);
}

QcChunkStore::WholeFile QcServerPersistence::playerFile(const QcWorldStorage &storage, const QcServerClient &client) const
{
    // Only joined players are saved, and their names are never empty (QcProfileValidation).
    Q_ASSERT(client.joined && !client.name.isEmpty());
    QcPlayerRecords::Record record = QcPlayerRecords::of(client, m_state.content());
    QcPlayerData &player = record.player;
    // A rider is saved where he would get out: the seat lies inside the block under the rail, and the
    // rider is not saved with the cart (decision E-383).
    if (const QcMinecart *cart = m_state.entities().minecarts().ridden(client.id)) {
        player.motion.position = QcServerMinecarts::exitSpot(m_state.world(), *cart);
        player.motion.velocity = {};
        player.motion.onGround = false;
    }
    if (record.lost > 0) {
        qCWarning(lcServer) << "Inventory of" << client.name << "is full:" << record.lost
                            << "items from the crafting grid and the stonecutter input were not saved";
    }
    return {QcPlayerStorage::relativePath(client.name), storage.playerBytes(player)};
}

std::pair<quint64, QcStatus> QcServerPersistence::submitSave()
{
    // Contents spilled since the last tick lie on the ground before the items are kept.
    m_blocks.dropSpills();
    // Every entity is in a loaded chunk, so each one is kept with its chunk.
    m_loading.loadStraysNow();
    QcStatus result;
    m_state.info().lastPlayed = QDateTime::currentDateTimeUtc();
    m_state.info().chunkLoaders = m_state.world().loaders().loaders();
    m_state.info().nextCreatureId = m_state.entities().creatures().nextUid();
    m_state.info().nextMapId = m_state.maps().nextId();
    QList<QcChunkStore::WholeFile> files{{QString::fromLatin1(QcWorldStorage::infoFileName), QcWorldStorage::infoBytes(m_state.info())}};
    // villages.json only after a change since it was last written.
    const quint64 villageChanges = m_state.world().villages().changes();
    if (m_villagesSaved != villageChanges) {
        files.append({QString::fromLatin1(QcWorldStorage::villagesFileName), QcWorldStorage::villagesBytes(m_state.world().villages().villages())});
        m_villagesSaved = villageChanges;
    }
    const QcResult<std::optional<QByteArray>> updates = blockUpdateBytes();
    if (updates) {
        files.append({QString::fromLatin1(QcWorldStorage::blockUpdatesFileName), *updates});
    } else {
        result = qcError(updates.error());
    }
    // The players with the world: an item between a chest and an inventory is never in both or none.
    for (const auto &client : m_state.clients()) {
        if (client->joined && m_storage) {
            files.append(playerFile(*m_storage, *client));
        }
    }
    // Only the maps changed since the last save (decision E-799).
    const QList<quint32> maps = m_state.maps().takeUnsaved();
    for (const quint32 map : maps) {
        const QcMapData *data = m_state.maps().find(map);
        if (!data) {
            continue;
        }
        QByteArray bytes = QcMapSaveCodec::write(*data, m_state.content());
        // A file the reader would refuse is not written (a map compresses far below the limit).
        if (bytes.size() > QcMapSaveCodec::maxBytes) {
            qCWarning(lcServer) << "Map" << map << "takes" << bytes.size() << "bytes, more than a map file may; not saved";
            continue;
        }
        files.append({QcMapSaveCodec::fileName(map), std::move(bytes)});
    }
    const quint64 id = m_io->save(m_loading.departuresForSave(), std::move(files));
    if (!maps.isEmpty()) {
        m_mapsBySave.insert(id, maps);
    }
    ++m_running;
    return {id, result};
}

void QcServerPersistence::noteResult(quint64 save, const QcStatus &status)
{
    m_running = std::max(0, m_running - 1);
    // The maps of a failed save are written again with the next one; written ones may leave memory.
    m_state.maps().finishSave(m_mapsBySave.take(save), bool(status));
    if (status) {
        m_failing = false;
        return;
    }
    if (!m_failing) {
        m_newFailure = status;
    }
    // What the failed save held may be missing on disk: the next save writes villages.json again.
    m_villagesSaved.reset();
    m_failing = true;
    qCWarning(lcServer) << "Saving failed:" << status.error();
}

QcStatus QcServerPersistence::save()
{
    if (!m_storage) {
        return {};
    }

    auto [id, result] = submitSave();
    m_io->waitForIdle();
    for (const QcChunkIo::SaveResult &done : m_io->takeSaves()) {
        noteResult(done.id, done.status);
        if (done.id == id && !done.status && result) {
            result = done.status;
        }
    }
    // A save the caller waits for is reported by the caller.
    if (!result) {
        m_newFailure.reset();
    }
    return result;
}

void QcServerPersistence::saveInBackground()
{
    if (!m_storage) {
        return;
    }
    if (const QcStatus collected = submitSave().second; !collected) {
        qCWarning(lcServer) << collected.error();
    }
}

std::optional<QcStatus> QcServerPersistence::takeNewFailure()
{
    if (m_io) {
        for (const QcChunkIo::SaveResult &done : m_io->takeSaves()) {
            noteResult(done.id, done.status);
        }
    }
    return std::exchange(m_newFailure, std::nullopt);
}

void QcServerPersistence::waitForStorage()
{
    if (m_io) {
        m_io->waitForIdle();
        // Results of what was waited for are noted at once.
        if (const std::optional<QcStatus> failure = takeNewFailure()) {
            m_newFailure = failure;
        }
    }
}

bool QcServerPersistence::manyUnsavedChunks() const
{
    return m_io && m_io->pendingChunks() > qsizetype(m_state.config().limits.unsavedChunks);
}

QcResult<std::optional<QByteArray>> QcServerPersistence::blockUpdateBytes() const
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
        qCWarning(lcServer) << "Saving only" << storable << "of" << updates.size() << "pending block updates";
        updates.resize(storable);
    }
    return QcWorldStorage::blockUpdatesBytes(updates);
}
