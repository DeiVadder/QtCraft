// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawning/QcMonsterCap.h"

#include <algorithm>
#include <cstdlib>

namespace {

// Whether `chunk` lies in the area around `centre`.
bool inArea(QcChunkPos centre, QcChunkPos chunk)
{
    return std::abs(chunk.x - centre.x) <= QcMonsterCap::reach && std::abs(chunk.z - centre.z) <= QcMonsterCap::reach;
}

} // namespace

QcMonsterCap::QcMonsterCap(int perArea, const QList<QcChunkPos> &playerChunks, const std::function<bool(QcChunkPos)> &loaded)
    : QcMonsterCap(perArea, playerChunks, loadedChunksOf(playerChunks, loaded))
{
}

QcMonsterCap::QcMonsterCap(int perArea, const QList<QcChunkPos> &playerChunks, qsizetype loadedChunks)
    : m_perArea(perArea)
    , m_worldCap(static_cast<int>(static_cast<qint64>(perArea) * loadedChunks / areaChunks))
{
    for (const QcChunkPos centre : playerChunks) {
        m_areas.append({centre, 0});
    }
}

qsizetype QcMonsterCap::loadedChunksOf(const QList<QcChunkPos> &playerChunks, const std::function<bool(QcChunkPos)> &loaded)
{
    // Chunks in the areas of several players count once: with the first player whose area holds them.
    qsizetype count = 0;
    for (qsizetype player = 0; player < playerChunks.size(); ++player) {
        const QcChunkPos centre = playerChunks.at(player);
        const auto counted = [&](QcChunkPos chunk) {
            return std::any_of(playerChunks.begin(), playerChunks.begin() + player, [chunk](QcChunkPos earlier) { return inArea(earlier, chunk); });
        };
        for (int dx = -reach; dx <= reach; ++dx) {
            for (int dz = -reach; dz <= reach; ++dz) {
                const QcChunkPos chunk{centre.x + dx, centre.z + dz};
                count += !counted(chunk) && loaded(chunk);
            }
        }
    }
    return count;
}

bool QcMonsterCap::Area::holds(QcChunkPos chunk) const
{
    return inArea(centre, chunk);
}

void QcMonsterCap::add(QcChunkPos chunk)
{
    m_count += 1;
    for (Area &area : m_areas) {
        area.count += area.holds(chunk);
    }
}

int QcMonsterCap::roomIn(QcChunkPos chunk) const
{
    int areaRoom = 0;
    for (const Area &area : m_areas) {
        if (area.holds(chunk)) {
            areaRoom = std::max(areaRoom, m_perArea - area.count);
        }
    }
    return std::max(0, std::min(areaRoom, m_worldCap - m_count));
}
