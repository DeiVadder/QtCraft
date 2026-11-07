// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcServerPersistence.h"

#include "common/content/loader/QcContentPackLoader.h"
#include "common/world/QcDayCycle.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/boats/QcServerBoats.h"
#include "server/chunks/QcChunkLoading.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerSetup.h"
#include "server/core/QcServerState.h"
#include "server/dimensions/QcDimensions.h"
#include "server/maps/QcMapSaveCodec.h"
#include "server/minecarts/QcServerMinecarts.h"
#include "server/mobs/QcServerMounts.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcPlayerRecords.h"
#include "server/storage/QcSaveFile.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The generator version a world of `config` is generated as (decision E-922): the configured one,
// for the showcase always the current one (it is not versioned).
int versionOf(const QcServerConfig &config)
{
    return config.generator == QLatin1StringView(QcWorldGenerators::showcase) ? QcGeneratorVersion::current : config.generatorVersion;
}

// A world option value as a message shows it: the number or true/false, "none" when it is missing.
QString optionText(const QJsonValue &value)
{
    if (value.isUndefined()) {
        return u"none"_s;
    }
    return value.toVariant().toString();
}

// Why the world's options `stored` are not the `given` ones: the first option (by name) that differs,
// with both values.
QString optionsDifference(const QJsonObject &stored, const QJsonObject &given)
{
    QStringList names = stored.keys() + given.keys();
    std::ranges::sort(names);
    for (const QString &name : std::as_const(names)) {
        if (stored.value(name) != given.value(name)) {
            return u"The world was made with the world option '%1' = %2, not %3"_s.arg(name, optionText(stored.value(name)), optionText(given.value(name)));
        }
    }
    return u"The world was made with other world generation options than given"_s;
}

// Where the player in a vehicle is saved: where he would get out, for he is not saved with it and
// comes back on foot. The seat of a cart lies inside the block under the rail (decision E-383), a
// boat puts him beside it (decision E-920), a creature on firm ground (decision E-1054).
std::optional<QcVec3> exitOfVehicle(const QcServerState &state, const QcServerClient &client)
{
    const QcEntities &entities = state.entities();
    if (const QcMinecart *cart = entities.minecarts().ridden(client.id)) {
        return QcServerMinecarts::exitSpot(state.world(), *cart);
    }
    if (const QcBoat *boat = entities.boats().drivenBy(client.id)) {
        return QcServerBoats::exitSpot(state.world(), *boat);
    }
    if (const QcCreature *mob = entities.creatures().riddenBy(client.id)) {
        return QcServerMounts::exitSpot(state.world(), *mob);
    }
    return std::nullopt;
}

} // namespace

QcServerPersistence::QcServerPersistence(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates, QcChunkLoading &loading)
    : m_state(state)
    , m_blocks(blocks)
    , m_updates(updates)
    , m_loading(loading)
{
    m_state.setDimensionStorage([this](QcServerDimension &dimension) { return openDimension(dimension); });
}

QcServerPersistence::~QcServerPersistence()
{
    // The world must not read or keep chunks through a storage that is gone.
    m_state.setDimensionStorage({});
    for (const std::unique_ptr<QcServerDimension> &dimension : m_state.dimensions().all()) {
        dimension->world().setStorage(nullptr, {});
    }
}

QcStatus QcServerPersistence::open()
{
    m_openWarnings.clear();
    noteWarnings(QcDimensions::definitionWarnings(m_state.content()));

    // The world is generated as its version or not at all (decision E-922).
    if (const int generates = m_state.world().generator().version(); generates != versionOf(m_state.config())) {
        return qcError(u"The world generator generates as version %1, not as the world's version %2"_s.arg(generates).arg(versionOf(m_state.config())));
    }

    // New worlds, saved or not, start in the morning; their generator's setup is still to run.
    m_state.info().worldTime = QcDayCycle::startTime;
    m_state.info().keepInventory = m_state.config().keepInventory;
    m_state.info().defaultGameMode = m_state.config().defaultGameMode.value_or(QcGameMode::Survival);
    m_state.info().generator = m_state.config().generator;
    m_state.info().generatorOptions = m_state.config().generatorOptions;
    m_state.info().worldOptions = m_state.config().worldOptions;
    m_state.info().startDimension = m_state.config().startDimension;
    m_state.info().generatorVersion = versionOf(m_state.config());
    m_state.info() = QcDimensions::withVersionsForNewWorld(m_state.content(), std::move(m_state.info()));
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
    if (const QcStatus mapped = applyVersionMap(m_state.dimensions().overworld(), m_state.config().worldDirectory); !mapped) {
        return mapped;
    }

    // A save writes world.json, the chunks and their entities as one (decision E-373): a world whose
    // setup is not done has nothing of it stored, so the setup simply runs.
    if (const QcStatus read = readBlockUpdates(storage); !read) {
        return read;
    }
    m_villagesSaved = restoreVillages(storage);
    restorePortals(storage.directory());

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
    noteWarnings(m_io->openWarnings());
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

QcStatus QcServerPersistence::openDimension(QcServerDimension &dimension)
{
    // A server without a world open yet (tools, tests of the state) has no storage to open.
    if (!m_io) {
        return {};
    }

    const QString directory = m_storage ? QcWorldStorage::dimensionDirectory(m_storage->directory(), dimension.definition()) : QString();
    auto io = std::make_unique<QcChunkIo>(directory, m_state.contentPointer(), m_io.get());
    QcResult<QHash<QcChunkPos, QcChunkStore::Stored>> catalog = io->open();
    if (!catalog) {
        return qcError(u"The dimension '%1' cannot be read: %2"_s.arg(dimension.name(), catalog.error()));
    }
    noteWarnings(io->openWarnings());
    dimension.world().setStorage(io.get(), std::move(*catalog));
    m_state.entities().creatures().setNextUid(m_state.info().nextCreatureId);

    std::optional<quint64> villagesSaved;
    bool setupDone = false;
    if (m_storage) {
        const QcWorldStorage storage(directory, m_state.contentPointer());
        QcResult<QcWorldStorage::DimensionFile> stored = storage.readDimensionFile();
        if (!stored) {
            dimension.world().setStorage(nullptr, {});
            return qcError(u"The dimension '%1' cannot be read: %2"_s.arg(dimension.name(), stored.error()));
        }
        setupDone = stored->setupDone;
        if (const QcStatus mapped = applyVersionMap(dimension, directory); !mapped) {
            dimension.world().setStorage(nullptr, {});
            return qcError(u"The dimension '%1' cannot be read: %2"_s.arg(dimension.name(), mapped.error()));
        }
        dimension.world().loaders().restore(stored->chunkLoaders);
        if (const QcStatus read = readBlockUpdates(storage); !read) {
            dimension.world().setStorage(nullptr, {});
            return read;
        }
        villagesSaved = restoreVillages(storage);
        restorePortals(directory);
    }
    dimensionStores().push_back({&dimension, std::move(io), villagesSaved});

    if (!setupDone) {
        QcServerSetup::applyContents(m_state, m_updates, dimension.world().generator().setup());
    }
    return {};
}

QcStatus QcServerPersistence::applyVersionMap(QcServerDimension &dimension, const QString &directory)
{
    const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(directory);
    if (!map) {
        return qcError(u"%1; without it the chunks generated before a generator switch would change"_s.arg(map.error()));
    }
    if (map->isEmpty()) {
        return {};
    }

    // The chunks of other versions are generated like the dimension's own chunks, only older.
    const QcDimensionDefinition &definition = dimension.definition();
    const QString type = QcDimensions::generatorTypeIn(m_state.info(), definition);
    const QString options = definition.isOverworld() ? m_state.info().generatorOptions : definition.generatorOptions;
    const auto create = [content = m_state.contentPointer(), seed = m_state.config().seed, type, options](int version) {
        return QcWorldGenerators::create(type, content, seed, options, version);
    };
    const QStringList before = dimension.world().generator().warnings();
    if (const QcStatus set = dimension.world().setGeneratorVersionMap(*map, create); !set) {
        return set;
    }
    // What the generators of the older versions could not resolve (the base's was reported when
    // it was made).
    QStringList added = dimension.world().generator().warnings();
    added.removeIf([&before](const QString &warning) { return before.contains(warning); });
    noteWarnings(added);
    return {};
}

std::optional<quint64> QcServerPersistence::restoreVillages(const QcWorldStorage &storage)
{
    QcVillages &villages = m_state.world().villages();
    const QcVillages::Limits &limits = villages.limits();
    // A plan never leaves its region, and a village grows (decision E-490) at most to limits.extent.
    const int extent = std::max(m_state.content().villageCatalog().settings().spacing, limits.extent);
    const QcVillage::ReadLimits read{extent, limits.beds, limits.workstations};
    const QcResult<QList<QcVillage>> stored = storage.readVillages(read);
    if (stored) {
        villages.restore(*stored);
        return villages.changes();
    }

    // The villages follow from the generator's plans again as their chunks load; what only the file
    // knew (claims, population) starts over. The next save writes a new file.
    villages.restore({});
    const QString path = QDir(storage.directory()).filePath(QString::fromLatin1(QcWorldStorage::villagesFileName));
    const QString copy = QcSaveFile::keepAside(path);
    if (copy.isEmpty()) {
        noteWarnings({u"%1; the villages start anew from the world generator, and the file could not be kept aside: the next save replaces it"_s.arg(
            stored.error())});
        return std::nullopt;
    }
    QFile::remove(path);
    noteWarnings({u"%1; the villages start anew from the world generator, the damaged file was kept as %2"_s.arg(stored.error(),
                                                                                                               QFileInfo(copy).fileName())});
    return std::nullopt;
}

void QcServerPersistence::restorePortals(const QString &directory)
{
    const QcResult<QList<QcPortalIndex::Entry>> stored = QcPortalIndex::read(directory);
    if (stored) {
        m_state.current().portals().restore(*stored);
        return;
    }
    // Portals light again and are found again as they are used; the next save writes a new file.
    noteWarnings({u"%1; the portals of the dimension are found again as they are used"_s.arg(stored.error())});
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
    if (info->generatorVersion != versionOf(m_state.config())) {
        return qcError(u"The world was made by world generator version %1, not %2"_s.arg(info->generatorVersion).arg(versionOf(m_state.config())));
    }
    if (info->generatorOptions != m_state.config().generatorOptions) {
        return qcError(u"The world was made with the options '%1', not '%2'"_s.arg(info->generatorOptions, m_state.config().generatorOptions));
    }
    if (info->worldOptions != m_state.config().worldOptions) {
        return qcError(optionsDifference(info->worldOptions, m_state.config().worldOptions));
    }

    m_state.info() = *info;
    // QcWorldLibrary writes world.json of a new world without the content, so without its dimensions.
    if (!m_state.info().setupDone) {
        m_state.info() = QcDimensions::withVersionsForNewWorld(m_state.content(), std::move(m_state.info()));
    }
    warnings.append(QcDimensions::versionWarnings(m_state.content(), m_state.info()));
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
        return std::unexpected(QcLoadFailure{file.error(), {}});
    }
    return m_storage->parsePlayer(name, *file);
}

QcChunkStore::WholeFile QcServerPersistence::playerFile(const QcWorldStorage &storage, QcServerClient &client)
{
    // Only joined players are saved, and their names are never empty (QcProfileValidation).
    Q_ASSERT(client.joined && !client.name.isEmpty());
    const QcServerState::Scope scope(m_state, m_state.dimensionOf(client));
    QcPlayerRecords::Record record = QcPlayerRecords::of(client, m_state.content());
    QcPlayerData &player = record.player;
    if (const std::optional<QcVec3> exit = exitOfVehicle(m_state, client)) {
        player.motion.position = *exit;
        player.motion.velocity = {};
        player.motion.onGround = false;
    }
    if (record.lost > 0) {
        qCWarning(lcServer) << "Inventory of" << client.name << "is full:" << record.lost
                            << "items from the crafting grid and the stonecutter input were not saved";
    }
    return {QcPlayerStorage::relativePath(client.name), storage.playerBytes(player)};
}

QcResult<QList<QcChunkStore::WholeFile>> QcServerPersistence::dimensionFiles(std::optional<quint64> villagesSaved) const
{
    QList<QcChunkStore::WholeFile> files;
    // villages.json only after a change since it was last written.
    if (villagesSaved != m_state.world().villages().changes()) {
        files.append({QString::fromLatin1(QcWorldStorage::villagesFileName), QcWorldStorage::villagesBytes(m_state.world().villages().villages())});
    }
    const QcResult<std::optional<QByteArray>> updates = blockUpdateBytes();
    if (!updates) {
        return qcError(updates.error());
    }
    files.append({QString::fromLatin1(QcWorldStorage::blockUpdatesFileName), *updates});
    // The lit portals (decision E-1023), once the dimension knew one.
    const QcPortalIndex &portals = m_state.current().portals();
    if (!portals.entries().isEmpty() || portals.changes() > 0) {
        files.append({QString::fromLatin1(QcPortalIndex::fileName), portals.bytes()});
    }
    return files;
}

QcStatus QcServerPersistence::handDimensions()
{
    QcStatus result;
    for (DimensionStore &store : dimensionStores()) {
        const QcServerState::Scope scope(m_state, *store.dimension);
        m_blocks.dropSpills();
        m_loading.loadStraysNow();
        QcResult<QList<QcChunkStore::WholeFile>> files = dimensionFiles(store.villagesSaved);
        if (files) {
            store.villagesSaved = m_state.world().villages().changes();
        } else {
            result = qcError(files.error());
            files = QList<QcChunkStore::WholeFile>();
        }
        files->append({QString::fromLatin1(QcWorldStorage::dimensionFileName),
                       QcWorldStorage::dimensionFileBytes({true, m_state.world().loaders().loaders()})});
        store.io->hand(m_loading.departuresForSave(), std::move(*files));
    }
    return result;
}

std::pair<quint64, QcStatus> QcServerPersistence::submitSave()
{
    // A command of a player elsewhere may ask for the save: what follows is the overworld's.
    const QcServerState::Scope scope(m_state, m_state.dimensions().overworld());
    // Contents spilled since the last tick lie on the ground before the items are kept.
    m_blocks.dropSpills();
    // Every entity is in a loaded chunk, so each one is kept with its chunk.
    m_loading.loadStraysNow();
    QcStatus result = handDimensions();
    m_state.info().lastPlayed = QDateTime::currentDateTimeUtc();
    m_state.info().chunkLoaders = m_state.world().loaders().loaders();
    m_state.info().nextCreatureId = m_state.entities().creatures().nextUid();
    m_state.info().nextMapId = m_state.maps().nextId();
    QList<QcChunkStore::WholeFile> files{{QString::fromLatin1(QcWorldStorage::infoFileName), QcWorldStorage::infoBytes(m_state.info())}};
    const QcResult<QList<QcChunkStore::WholeFile>> own = dimensionFiles(m_villagesSaved);
    if (own) {
        m_villagesSaved = m_state.world().villages().changes();
        files.append(*own);
    } else {
        result = qcError(own.error());
    }
    // The players with the world: an item between a chest and an inventory is never in both or none.
    for (const auto &client : m_state.allClients()) {
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
    QList<QcChunkIo *> linked;
    for (const DimensionStore &store : dimensionStores()) {
        linked.append(store.io.get());
    }
    const quint64 id = m_io->save(m_loading.departuresForSave(), std::move(files), std::move(linked));
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
    m_state.maps().finishSave(m_mapsBySave.take(save), static_cast<bool>(status));
    if (status) {
        m_failing = false;
        return;
    }
    if (!m_failing) {
        m_newFailure = status;
    }
    // What the failed save held may be missing on disk: the next save writes villages.json again.
    m_villagesSaved.reset();
    for (DimensionStore &store : dimensionStores()) {
        store.villagesSaved.reset();
    }
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
    qsizetype pending = m_io ? m_io->pendingChunks() : 0;
    for (const DimensionStore &store : dimensionStores()) {
        pending += store.io->pendingChunks();
    }
    return pending > static_cast<qsizetype>(m_state.config().limits.unsavedChunks);
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

    const qsizetype storable = std::min(static_cast<qsizetype>(m_state.config().limits.savedBlockUpdates),
        static_cast<qsizetype>(QcWorldStorage::maxStoredBlockUpdates));
    if (updates.size() > storable) {
        qCWarning(lcServer) << "Saving only" << storable << "of" << updates.size() << "pending block updates";
        updates.resize(storable);
    }
    return QcWorldStorage::blockUpdatesBytes(updates);
}
