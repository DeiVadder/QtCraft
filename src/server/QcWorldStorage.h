#pragma once

#include "QcPackReference.h"

#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcInventory.h"
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
};

struct QcPlayerData
{
    QcEntityMotion motion;
    float yaw = 0.0f;
    float pitch = 0.0f;
    QcInventory inventory;
    double health = 20.0;
};

// Reads and writes one world directory: world.json, modified chunks in region files
// (regions/r.<x>.<z>.qcr, 32 x 32 chunks each) and players (players/<name>.qcp). Only modified chunks
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

    static QcChunkPos regionOf(QcChunkPos chunk);

private:
    QString regionPath(QcChunkPos region) const;
    QString playerPath(const QString &playerName) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
