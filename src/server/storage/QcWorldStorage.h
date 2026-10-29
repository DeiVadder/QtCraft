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
    static constexpr auto villagesFileName = "villages.json";

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
    [[nodiscard]] QcPlayerStorage::Result readPlayer(const QString &playerName) const { return m_players.read(playerName); }
    [[nodiscard]] QcStatus writePlayer(const QString &playerName, const QcPlayerData &player) const { return m_players.write(playerName, player); }
    QByteArray playerBytes(const QcPlayerData &player) const { return m_players.bytes(player); }
    [[nodiscard]] QcPlayerStorage::Result parsePlayer(const QString &playerName, const std::optional<QByteArray> &file) const
    {
        return m_players.parse(playerName, file);
    }

    // Positions waiting for a neighbour update (blockupdates.qce). Empty without error when nothing
    // was saved yet. At most maxStoredBlockUpdates are written; the rest is reported as an error.
    static constexpr quint32 maxStoredBlockUpdates = 4 * 1024 * 1024;
    [[nodiscard]] QcResult<QList<QcBlockPos>> readBlockUpdates() const;
    // The contents of blockupdates.qce for `positions` (none: no file).
    [[nodiscard]] static QcResult<std::optional<QByteArray>> blockUpdatesBytes(const QList<QcBlockPos> &positions);

    // The villages of the world (villages.json, decision E-403); empty without error when none was
    // saved yet; an error when the file cannot be read or is no JSON object. Entries that are not
    // plausible are left out, spot lists cut to their limits (QcVillage::fromJson).
    [[nodiscard]] QcResult<QList<QcVillage>> readVillages(const QcVillage::ReadLimits &limits) const;
    static QByteArray villagesBytes(const QList<QcVillage> &villages);

    static QcChunkPos regionOf(QcChunkPos chunk) { return QcRegionFile::regionOf(chunk); }

private:
    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcPlayerStorage m_players;
};
