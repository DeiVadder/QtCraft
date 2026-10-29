// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cstdlib>

// The spawn point of the terrain generator: the search over growing squares around the origin.
namespace {

constexpr int spawnSearchRadius = 512;

} // namespace

class QcTerrainGenerator::SpawnSearch
{
public:
    explicit SpawnSearch(const QcTerrainGenerator &terrain)
        : m_terrain(terrain)
    {
    }

    // Spawn candidate on the square ring of `radius` around the origin, if any.
    [[nodiscard]] std::optional<QcBlockPos> onRing(int radius)
    {
        for (int dx = -radius; dx <= radius; dx += 8) {
            for (int dz = -radius; dz <= radius; dz += 8) {
                if (std::max(std::abs(dx), std::abs(dz)) != radius) {
                    continue;
                }
                const Column info = m_terrain.column(dx, dz);
                if (fits(dx, dz, info)) {
                    return QcBlockPos{dx, info.height, dz};
                }
            }
        }
        return std::nullopt;
    }

private:
    // Open, dry and not too high ground at a column.
    [[nodiscard]] bool fits(int x, int z, const Column &column)
    {
        const int sea = m_terrain.seaLevel();
        const bool candidate = column.landFactor > 0.99 && column.height > sea + 2 && column.height < sea + 30 && column.overhang < 1.0;
        if (!candidate || m_terrain.isCave(x, column.height - 1, z)) {
            return false;
        }

        const QcBlockPos pos{x, column.height - 1, z};
        if (!m_chunk || m_chunk->pos() != pos.chunk()) {
            m_chunk = m_terrain.generate(pos.chunk());
        }
        return m_chunk->highestNonAir(pos.localX(), pos.localZ()) == pos.y
               && m_terrain.isNaturalGround(m_chunk->block(pos.localX(), pos.y, pos.localZ()));
    }

    const QcTerrainGenerator &m_terrain;
    // The last chunk generated for the search.
    std::shared_ptr<QcChunk> m_chunk;
};

QcBlockPos QcTerrainGenerator::spawnPoint() const
{
    // Nearest dry, not too high open ground on a spiral of growing squares around the origin: no
    // tree crown, cave or plant above the surface block, and no overhanging mountain land.
    SpawnSearch search(*this);
    for (int radius = 0; radius <= spawnSearchRadius; radius += 8) {
        if (const std::optional<QcBlockPos> spawn = search.onRing(radius)) {
            return *spawn;
        }
    }
    return {0, std::max(column(0, 0).height, seaLevel()), 0};
}
