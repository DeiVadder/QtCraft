#pragma once

#include "QcPackReference.h"

#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcInventory.h"
#include "QcContainerManager.h"
#include "QcFallingBlockManager.h"
#include "QcItemEntityManager.h"
#include "QcMachineManager.h"
#include "QcPlantManager.h"
#include "QcResult.h"

#include <QDateTime>
#include <QHash>
#include <QString>

#include <memory>

// Metadata of a saved world (world.json).
struct QcWorldInfo
{
    static constexpr int formatVersion = 1;

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
};

// Dropped items, growing saplings and falling blocks of a world (entities.qce).
struct QcWorldEntities
{
    QList<QcItemEntityManager::Drop> items;
    QList<QcPlantManager::Sapling> saplings;
    // Since format 2 of entities.qce.
    QList<QcFallingBlockManager::Falling> fallingBlocks;
};

struct QcPlayerData
{
    QcEntityMotion motion;
    float yaw = 0.0f;
    float pitch = 0.0f;
    QcInventory inventory;
    double health = 20.0;
    // Players saved before hunger existed come back full.
    int food = QcItemDefinition::maxFood;
    double exhaustion = 0.0;
};

// Reads and writes one world directory: world.json, modified chunks in region files
// (regions/r.<x>.<z>.qcr, 32 x 32 chunks each), players (players/<name>.qcp) and dropped items with
// growing saplings (entities.qce), machines (machines.qce), chests (containers.qce) and pending
// neighbour updates (blockupdates.qce). Only modified chunks
// are stored; everything else is regenerated from the seed. Blocks and items are stored by name, so
// content changes between sessions do not scramble a world. Every file carries a magic number, a
// format version and a CRC; damaged files make loading fail with a message instead of crashing.
class QcWorldStorage
{
public:
    static constexpr int regionSize = 32;

    QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    const QString &directory() const { return m_directory; }

    QcResult<QcWorldInfo> readInfo() const;
    QcStatus writeInfo(const QcWorldInfo &info) const;

    // Loads every stored chunk. Unknown block names become air and are listed in `warnings`.
    QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> readChunks(QStringList &warnings) const;
    // Rewrites the region files that contain any of the given chunks with all chunks of those regions.
    QcStatus writeChunks(const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> &chunks) const;

    // std::nullopt without error when the player has never been saved.
    QcResult<std::optional<QcPlayerData>> readPlayer(const QString &playerName, QStringList &warnings) const;
    QcStatus writePlayer(const QString &playerName, const QcPlayerData &player) const;

    // Empty without error when nothing was saved yet. Unknown items are dropped with a warning.
    QcResult<QcWorldEntities> readEntities(QStringList &warnings) const;
    QcStatus writeEntities(const QcWorldEntities &entities) const;

    // Machines with their contents (machines.qce). Empty without error when nothing was saved yet.
    // Unknown items are dropped with a warning.
    QcResult<QList<QcMachineManager::Machine>> readMachines(QStringList &warnings) const;
    QcStatus writeMachines(const QList<QcMachineManager::Machine> &machines) const;

    // Storage blocks with their contents (containers.qce). Empty without error when nothing was saved
    // yet. Unknown items are dropped with a warning.
    QcResult<QList<QcContainerManager::Container>> readContainers(QStringList &warnings) const;
    QcStatus writeContainers(const QList<QcContainerManager::Container> &containers) const;

    // Positions waiting for a neighbour update (blockupdates.qce). Empty without error when nothing
    // was saved yet. At most maxStoredBlockUpdates are written; the rest is reported as an error.
    static constexpr quint32 maxStoredBlockUpdates = 4 * 1024 * 1024;
    QcResult<QList<QcBlockPos>> readBlockUpdates() const;
    QcStatus writeBlockUpdates(const QList<QcBlockPos> &positions) const;

    static QcChunkPos regionOf(QcChunkPos chunk);

private:
    QString regionPath(QcChunkPos region) const;
    QString playerPath(const QString &playerName) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
