// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "common/gameplay/QcGameMode.h"
#include "common/world/QcChunk.h"
#include "common/world/QcWorldInfo.h"
#include "server/chunks/QcChunkLoaders.h"
#include "server/entities/QcHurtWindow.h"
#include "server/storage/QcPlayerStorage.h"
#include "server/storage/QcRegionFile.h"
#include "server/villages/QcVillage.h"
#include "server/weather/QcWeatherCycle.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QString>

#include <memory>

/*! \brief Reads and writes the files of one world directory besides its chunks: world.json, players
    (players/<name>-<hash>.qcp) and pending neighbour updates (blockupdates.qce).

    The chunks (blocks, block entities and entities in indexed region files) are QcChunkStore's;
    a save writes world.json and blockupdates.qce together with the chunks through its journal
    (infoBytes, blockUpdatesBytes). Blocks and items are stored by name, so content changes
    between sessions do not scramble a world. Every binary file carries a magic number, a format
    version and a CRC; damaged files make loading fail with a message instead of crashing. */
class QcWorldStorage
{
public:
    /*! The side of a region in chunks (QcRegionFile::regionSize). */
    static constexpr int regionSize = QcRegionFile::regionSize;
    /*! The names of the world's whole files in its directory. \qcshared */
    static constexpr auto infoFileName = "world.json";
    static constexpr auto blockUpdatesFileName = "blockupdates.qce";
    static constexpr auto villagesFileName = "villages.json";
    static constexpr auto dimensionFileName = "dimension.json";

    QcWorldStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the world directory. */
    const QString &directory() const { return m_directory; }
    /*! The folder of the dimensions other than the overworld in a world directory. */
    static constexpr auto dimensionsFolder = "dimensions";
    /*! Returns the directory that holds the chunks of \a dimension in the world directory
        \a worldDirectory (decisions E-978, E-988): the world directory itself for the overworld,
        as before dimensions, else dimensions/<namespace>/<name> (\c {qtcraft:nether} in
        dimensions/qtcraft/nether); a part Windows reserves as a device name (\c {con},
        \c {COM1}, any case) gets a '~', which no dimension name holds. world.json, the players,
        the maps and the lock file belong to the world and stay in the world directory.

        It takes a definition, so only a name the content checked becomes a path: keys of
        world.json and names of player files never do. */
    static QString dimensionDirectory(const QString &worldDirectory, const QcDimensionDefinition &dimension);

    /*! Returns the world's info read from world.json, or the error. */
    [[nodiscard]] QcResult<QcWorldInfo> readInfo() const;
    /*! Returns the format version world.json names; 0 when it cannot be read. */
    int storedFormatVersion() const;
    /*! Writes \a info to world.json (creating the directory). Returns an error when it cannot be
        written. */
    [[nodiscard]] QcStatus writeInfo(const QcWorldInfo &info) const;
    /*! Returns the contents of world.json for \a info. */
    static QByteArray infoBytes(const QcWorldInfo &info);

    /*! Returns the file name of the player \a playerName (QcPlayerStorage::fileName). */
    static QString playerFileName(const QString &playerName) { return QcPlayerStorage::fileName(playerName); }
    /*! Returns the player \a playerName read from his file (QcPlayerStorage::read). */
    [[nodiscard]] QcPlayerStorage::Result readPlayer(const QString &playerName) const { return m_players.read(playerName); }
    /*! Writes \a player to the file of \a playerName (QcPlayerStorage::write). Returns an error
        when it cannot be written. */
    [[nodiscard]] QcStatus writePlayer(const QString &playerName, const QcPlayerData &player) const { return m_players.write(playerName, player); }
    /*! Returns the contents of the file of \a player (QcPlayerStorage::bytes). */
    QByteArray playerBytes(const QcPlayerData &player) const { return m_players.bytes(player); }
    /*! Returns the player \a playerName read from the contents \a file of his file
        (QcPlayerStorage::parse). */
    [[nodiscard]] QcPlayerStorage::Result parsePlayer(const QString &playerName, const std::optional<QByteArray> &file) const
    {
        return m_players.parse(playerName, file);
    }

    /*! Positions waiting for a neighbour update (blockupdates.qce). Empty without error when
        nothing was saved yet. At most maxStoredBlockUpdates are written; the rest is reported as
        an error. */
    static constexpr quint32 maxStoredBlockUpdates = 4 * 1024 * 1024;
    /*! Returns the positions waiting for a neighbour update (blockupdates.qce); empty without
        error when nothing was saved yet. */
    [[nodiscard]] QcResult<QList<QcBlockPos>> readBlockUpdates() const;
    /*! Returns the contents of blockupdates.qce for \a positions (none: no file). */
    [[nodiscard]] static QcResult<std::optional<QByteArray>> blockUpdatesBytes(const QList<QcBlockPos> &positions);

    /*! Returns the villages of the world (villages.json, decision E-403); empty without error
        when none was saved yet; an error when the file cannot be read or is no JSON object.
        Entries that are not plausible are left out, spot lists cut to \a limits
        (QcVillage::fromJson). */
    [[nodiscard]] QcResult<QList<QcVillage>> readVillages(const QcVillage::ReadLimits &limits) const;
    /*! Returns the contents of villages.json for \a villages. */
    static QByteArray villagesBytes(const QList<QcVillage> &villages);

    /*! \brief What dimension.json keeps of a dimension other than the overworld, whose counterparts
        are in world.json for the overworld (decision E-988). \qcfields */
    struct DimensionFile
    {
        bool setupDone = false;
        QList<QcPlacedChunkLoader> chunkLoaders;
    };
    /*! Returns dimension.json of this directory; with no such file a dimension that was never
        saved; an error when the file cannot be read or is no JSON object. */
    [[nodiscard]] QcResult<DimensionFile> readDimensionFile() const;
    /*! Returns the contents of dimension.json for \a file. */
    static QByteArray dimensionFileBytes(const DimensionFile &file);

    /*! Returns the region that holds \a chunk. */
    static QcChunkPos regionOf(QcChunkPos chunk) { return QcRegionFile::regionOf(chunk); }

private:
    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcPlayerStorage m_players;
};
