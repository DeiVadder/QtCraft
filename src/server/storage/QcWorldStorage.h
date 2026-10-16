#pragma once

#include "QcPackReference.h"

#include "QcChunk.h"
#include "QcChunkLoaders.h"
#include "QcContentRegistry.h"
#include "QcGameMode.h"
#include "QcPlayerStorage.h"
#include "QcRegionFile.h"
#include "QcResult.h"
#include "QcWeatherCycle.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QString>

#include <memory>

// Metadata of a saved world (world.json). Format 5 (stage 31.3) keeps chunks in indexed region files
// read on demand, saved as one through a journal (decision E-373); format 6 (stage 32.1) saves
// minecarts with the entities of their chunk (decision E-382), format 7 (stage 32.2) also the slots of
// container carts (decision E-386), format 8 (stage 34) burning fires with their chunk (decision E-392). Worlds of older development versions
// are refused, not converted (decision E-356, QtCraft has not been released).
struct QcWorldInfo
{
    static constexpr int formatVersion = 8;
    static constexpr int oldestFormatVersion = 8;
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
    // Game rule: fire spreads to flammable blocks and burns them away (decision E-392); off, fires
    // only burn where they are until they go out.
    bool fireSpread = true;
    // Game rule: pistons that allow it are also powered through the block above them, noticed only
    // with a block update next to them (quasi-connectivity, decision E-338).
    bool quasiConnectivity = true;
    // Game rule: the weather changes by itself (decision E-395); off, it stays as it is until
    // /weather changes it.
    bool weatherCycle = true;
    // The weather and its course (world.json "weather"; a world without it begins with a clear spell).
    QcWeatherState weather;
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
    // Game rule (decision E-377): the chunks around the spawn column within this many chunks minus
    // one stay loaded (0: none, 1: the spawn chunk, 2: 3 x 3 chunks …).
    int spawnChunkRadius = 0;
    static constexpr int maxSpawnChunkRadius = 16;
    // Chunks kept loaded by /forceload (decision E-377), in the order they were added.
    QList<QcChunkPos> forcedChunks;
    // Every chunk loader block of the world in the order they were placed (decision E-377).
    QList<QcChunkLoaders::Loader> chunkLoaders;
};

// Reads and writes the files of one world directory besides its chunks: world.json, players
// (players/<name>-<hash>.qcp) and pending neighbour updates (blockupdates.qce). The chunks (blocks,
// block entities and entities in indexed region files) are QcChunkStore's; a save writes world.json
// and blockupdates.qce together with the chunks through its journal (infoBytes, blockUpdatesBytes).
// Blocks and items are stored by name, so content changes between sessions do not scramble a world.
// Every binary file carries a magic number, a format version and a CRC; damaged files make loading
// fail with a message instead of crashing.
class QcWorldStorage
{
public:
    static constexpr int regionSize = QcRegionFile::regionSize;
    static constexpr auto infoFileName = "world.json";
    static constexpr auto blockUpdatesFileName = "blockupdates.qce";

    QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    const QString &directory() const { return m_directory; }

    [[nodiscard]] QcResult<QcWorldInfo> readInfo() const;
    // The format version world.json names; 0 when it cannot be read.
    int storedFormatVersion() const;
    [[nodiscard]] QcStatus writeInfo(const QcWorldInfo &info) const;
    // The contents of world.json for `info`.
    static QByteArray infoBytes(const QcWorldInfo &info);

    // Player files (QcPlayerStorage).
    static QString playerFileName(const QString &playerName) { return QcPlayerStorage::fileName(playerName); }
    [[nodiscard]] QcResult<std::optional<QcPlayerData>> readPlayer(const QString &playerName, QStringList &warnings) const
    {
        return m_players.read(playerName, warnings);
    }
    [[nodiscard]] QcStatus writePlayer(const QString &playerName, const QcPlayerData &player) const { return m_players.write(playerName, player); }
    QByteArray playerBytes(const QcPlayerData &player) const { return m_players.bytes(player); }
    [[nodiscard]] QcResult<std::optional<QcPlayerData>> parsePlayer(const QString &playerName, const std::optional<QByteArray> &file, QStringList &warnings) const
    {
        return m_players.parse(playerName, file, warnings);
    }

    // Positions waiting for a neighbour update (blockupdates.qce). Empty without error when nothing
    // was saved yet. At most maxStoredBlockUpdates are written; the rest is reported as an error.
    static constexpr quint32 maxStoredBlockUpdates = 4 * 1024 * 1024;
    [[nodiscard]] QcResult<QList<QcBlockPos>> readBlockUpdates() const;
    // The contents of blockupdates.qce for `positions` (none: no file).
    [[nodiscard]] static QcResult<std::optional<QByteArray>> blockUpdatesBytes(const QList<QcBlockPos> &positions);

    static QcChunkPos regionOf(QcChunkPos chunk) { return QcRegionFile::regionOf(chunk); }

private:
    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcPlayerStorage m_players;
};
