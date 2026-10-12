#pragma once

#include "QcPackReference.h"

#include "QcBlockEntityStorage.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcEntityStorage.h"
#include "QcGameMode.h"
#include "QcPlayerStorage.h"
#include "QcRegionStorage.h"
#include "QcResult.h"
#include "QcStandStorage.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>

#include <memory>

// Metadata of a saved world (world.json). Format 2 adds the generator type, whether its one-time
// setup ran and the game rules daylightCycle and mobSpawning (decision E-221); format 1 files load
// as terrain worlds that are set up, with both rules on.
struct QcWorldInfo
{
    static constexpr int formatVersion = 2;
    static constexpr int oldestFormatVersion = 1;
    static constexpr auto defaultGenerator = "terrain";

    QString name;
    qint64 seed = 0;
    QDateTime created;
    QDateTime lastPlayed;
    // Seconds since the world was created, drives the day/night cycle.
    double worldTime = 0.0;
    // Content packs (base game and mods) used when the world was last saved.
    QList<QcPackReference> packs;
    // Game rule: players keep their inventory when they die instead of dropping it.
    bool keepInventory = true;
    // Game rules: the time of day runs, creatures spawn by themselves.
    bool daylightCycle = true;
    bool mobSpawning = true;
    // World generator type (QcWorldGenerators), its options (the layers of a flat world) and whether
    // its one-time setup has run.
    QString generator = QString::fromLatin1(defaultGenerator);
    QString generatorOptions;
    bool setupDone = true;
    // Game mode of players who join the world for the first time (decision E-282), chosen when the
    // world is created; worlds without the key are survival worlds.
    QcGameMode defaultGameMode = QcGameMode::Survival;
    // A mode name this version does not know (from a newer game): the world plays as survival but
    // keeps the name when saved, until /defaultgamemode sets another mode.
    QString unknownGameMode;
};

// Reads and writes one world directory: world.json, modified chunks in region files
// (regions/r.<x>.<z>.qcr, 32 x 32 chunks each, QcRegionStorage), players (players/<name>-<hash>.qcp) and dropped items with
// growing saplings (entities.qce), animals (mobs.qce, both QcEntityStorage), machines (machines.qce), chests
// (containers.qce, both QcBlockEntityStorage) and pending neighbour updates (blockupdates.qce). Only modified chunks
// are stored; everything else is regenerated from the seed. Blocks and items are stored by name, so
// content changes between sessions do not scramble a world. Every file carries a magic number, a
// format version and a CRC; damaged files make loading fail with a message instead of crashing.
class QcWorldStorage
{
public:
    static constexpr int regionSize = QcRegionStorage::regionSize;

    QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    const QString &directory() const { return m_directory; }

    QcResult<QcWorldInfo> readInfo() const;
    QcStatus writeInfo(const QcWorldInfo &info) const;

    // Loads every stored chunk. Unknown block names become air and are listed in `warnings`.
    QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> readChunks(QStringList &warnings) const { return m_regions.read(warnings); }
    // Rewrites the region files that contain any of the given chunks with all chunks of those regions.
    QcStatus writeChunks(const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> &chunks) const { return m_regions.write(chunks); }

    // Player files (QcPlayerStorage).
    static QString playerFileName(const QString &playerName) { return QcPlayerStorage::fileName(playerName); }
    QcResult<std::optional<QcPlayerData>> readPlayer(const QString &playerName, QStringList &warnings) const
    {
        return m_players.read(playerName, warnings);
    }
    QcStatus writePlayer(const QString &playerName, const QcPlayerData &player) const { return m_players.write(playerName, player); }

    // Empty without error when nothing was saved yet. Unknown items are dropped with a warning.
    QcResult<QcWorldEntities> readEntities(QStringList &warnings) const { return m_entities.readEntities(warnings); }
    QcStatus writeEntities(const QcWorldEntities &entities) const { return m_entities.writeEntities(entities); }

    // Persistent creatures (mobs.qce). Empty without error when nothing was saved yet. Unknown kinds
    // are dropped with a warning.
    static constexpr quint32 maxStoredPopulatedChunks = QcEntityStorage::maxStoredPopulatedChunks;
    QcResult<QcWorldMobs> readMobs(QStringList &warnings) const { return m_entities.readMobs(warnings); }
    QcStatus writeMobs(const QcWorldMobs &mobs) const { return m_entities.writeMobs(mobs); }

    // Machines with their contents (machines.qce). Empty without error when nothing was saved yet.
    // Unknown items are dropped with a warning.
    QcResult<QList<QcMachineManager::Machine>> readMachines(QStringList &warnings) const { return m_blockEntities.readMachines(warnings); }
    QcStatus writeMachines(const QList<QcMachineManager::Machine> &machines) const { return m_blockEntities.writeMachines(machines); }

    // Storage blocks with their contents (containers.qce). Empty without error when nothing was saved
    // yet. Unknown items are dropped with a warning.
    QcResult<QList<QcContainerManager::Container>> readContainers(QStringList &warnings) const
    {
        return m_blockEntities.readContainers(warnings);
    }
    QcStatus writeContainers(const QList<QcContainerManager::Container> &containers) const
    {
        return m_blockEntities.writeContainers(containers);
    }

    // Armour stands (stands.qce, decision E-238). Empty when nothing was saved yet; a damaged file
    // is kept aside and only warned about.
    QList<QcArmourStandManager::Stand> readStands(QStringList &warnings) const { return m_stands.read(warnings); }
    QcStatus writeStands(const QList<QcArmourStandManager::Stand> &stands) const { return m_stands.write(stands); }

    // Positions waiting for a neighbour update (blockupdates.qce). Empty without error when nothing
    // was saved yet. At most maxStoredBlockUpdates are written; the rest is reported as an error.
    static constexpr quint32 maxStoredBlockUpdates = 4 * 1024 * 1024;
    QcResult<QList<QcBlockPos>> readBlockUpdates() const;
    QcStatus writeBlockUpdates(const QList<QcBlockPos> &positions) const;

    static QcChunkPos regionOf(QcChunkPos chunk) { return QcRegionStorage::regionOf(chunk); }

private:
    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcRegionStorage m_regions;
    QcEntityStorage m_entities;
    QcBlockEntityStorage m_blockEntities;
    QcStandStorage m_stands;
    QcPlayerStorage m_players;
};
