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

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QString>

#include <memory>

// Metadata of a saved world (world.json). Format 4 (stage 31.2) keeps entities per chunk (decision
// E-366), format 3 (stage 31.1) block entities (decision E-358); worlds of older development
// versions are refused, not converted (decision E-356, QtCraft has not been released).
struct QcWorldInfo
{
    static constexpr int formatVersion = 4;
    static constexpr int oldestFormatVersion = 4;
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
    // Game rule: blocks get random ticks (growing plants, crops, soil; decision E-295).
    bool randomTicks = true;
    // Game rule: pistons that allow it are also powered through the block above them, noticed only
    // with a block update next to them (quasi-connectivity, decision E-338).
    bool quasiConnectivity = true;
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
    // The world's simulation limits (decision E-349) as written in world.json ("limits"), only the
    // keys an operator set; a single-player world plays with them, a dedicated server ignores them.
    QJsonObject limits;
};

// Reads and writes one world directory: world.json, modified chunks in region files
// (regions/r.<x>.<z>.qcr, 32 x 32 chunks each, QcRegionStorage), block entities per chunk in a file per
// region (blockentities/r.<x>.<z>.qcb, QcBlockEntityStorage), entities per chunk in a file per region
// (entities/r.<x>.<z>.qen, QcEntityStorage), players (players/<name>-<hash>.qcp) and pending neighbour
// updates (blockupdates.qce). Only modified chunks are stored;
// everything else is regenerated from the seed. Blocks and items are stored by name, so
// content changes between sessions do not scramble a world. Every file carries a magic number, a
// format version and a CRC; damaged files make loading fail with a message instead of crashing.
class QcWorldStorage
{
public:
    static constexpr int regionSize = QcRegionStorage::regionSize;

    QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    const QString &directory() const { return m_directory; }

    QcResult<QcWorldInfo> readInfo() const;
    // The format version world.json names; 0 when it cannot be read.
    int storedFormatVersion() const;
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

    // Entities per chunk (QcEntityStorage, decision E-366); empty without error when nothing was saved.
    QcResult<QcWorldEntityRecords> readEntities(QStringList &warnings) const { return m_entities.read(warnings); }
    QString damagedEntityFile() const { return m_entities.damagedFile(); }
    QcStatus writeEntities(const QcWorldEntityRecords &entities) const { return m_entities.write(entities); }

    // Block entities per chunk (QcBlockEntityStorage, decision E-358).
    QcResult<QList<QcBlockEntityRecord>> readBlockEntities(QStringList &warnings) const { return m_blockEntities.read(warnings); }
    QString damagedBlockEntityFile() const { return m_blockEntities.damagedFile(); }
    QcStatus writeBlockEntities(const QList<QcBlockEntityRecord> &records) const { return m_blockEntities.write(records); }

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
    QcPlayerStorage m_players;
};
