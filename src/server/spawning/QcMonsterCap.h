// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

#include <QList>

#include <functional>

// How many monsters may live around the players (decision E-836), by the original game's formula:
// every living player spans an area of 17 × 17 chunks around its own; the world as a whole holds at
// most `perArea` monsters (QcServerLimits monstersPer289Chunks, 70) for every 289 loaded chunks
// of all areas together, so players far apart each bring a full share and players close together
// share one. On top of that a monster appears in a chunk only while some player whose area holds the
// chunk has fewer than `perArea` monsters in its own area, so one crowded player does not take the
// share of another.
class QcMonsterCap
{
public:
    // Chunks from a player's chunk to the edge of its area, and the chunks of one area.
    static constexpr int reach = 8;
    static constexpr int areaChunks = (2 * reach + 1) * (2 * reach + 1);

    // The areas around the chunks of the living players; `loaded` tells which chunks count.
    QcMonsterCap(int perArea, const QList<QcChunkPos> &playerChunks, const std::function<bool(QcChunkPos)> &loaded);
    // The same with the number of loaded chunks of the areas already counted (loadedChunksOf).
    QcMonsterCap(int perArea, const QList<QcChunkPos> &playerChunks, qsizetype loadedChunks);
    // The loaded chunks of all areas together, each once.
    static qsizetype loadedChunksOf(const QList<QcChunkPos> &playerChunks, const std::function<bool(QcChunkPos)> &loaded);

    // Counts one monster in `chunk`: the ones alive at the start and every one that appears.
    void add(QcChunkPos chunk);
    // Monsters that may still appear in `chunk`: room in the world and in the emptiest area holding
    // it; 0 outside every area.
    int roomIn(QcChunkPos chunk) const;
    // Whether any monster may still appear anywhere.
    bool hasRoom() const { return m_count < m_worldCap; }

    // The world's cap (perArea for every areaChunks loaded chunks of the areas) and its monsters.
    int worldCap() const { return m_worldCap; }
    int count() const { return m_count; }
    // The monsters in the area of the player at `index` of the constructor's list.
    int countAround(qsizetype index) const { return m_areas.at(index).count; }

private:
    struct Area
    {
        QcChunkPos centre;
        int count = 0;

        bool holds(QcChunkPos chunk) const;
    };

    int m_perArea = 0;
    int m_worldCap = 0;
    int m_count = 0;
    QList<Area> m_areas;
};
