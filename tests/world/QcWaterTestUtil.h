#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>

// Helpers of the tests of rivers and deltas (decisions E-840, E-850): generated chunks around a
// spot, and the water in them that would flow when they load.
namespace QcWaterTestUtil {

// The generated chunks of a square of 3 × 3 chunks, looked up by world coordinates.
class ChunkArea
{
public:
    ChunkArea(const QcTerrainGenerator &generator, QcChunkPos centre)
        : m_centre(centre)
    {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                m_chunks.append(generator.generate({centre.x + dx, centre.z + dz}));
            }
        }
    }

    [[nodiscard]] QcBlockId block(int x, int y, int z) const
    {
        const QcBlockPos pos{x, y, z};
        const QcChunkPos chunk = pos.chunk();
        return m_chunks[(chunk.z - m_centre.z + 1) * 3 + (chunk.x - m_centre.x + 1)]->block(pos.localX(), y, pos.localZ());
    }

private:
    QcChunkPos m_centre;
    QList<std::shared_ptr<QcChunk>> m_chunks;
};

// Every water block of the area's middle chunk beside or above air that no cave carved (a wall or
// a sheet of water that would flow when the chunk loads), also across the chunk's borders.
[[nodiscard]] inline QStringList openWater(const QcTerrainGenerator &generator, const ChunkArea &area, QcChunkPos centre)
{
    static constexpr std::array<std::array<int, 3>, 5> sides{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}}};
    QStringList open;
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int x = centre.x * QcWorld::chunkSize + lx;
            const int z = centre.z * QcWorld::chunkSize + lz;
            for (int y = 1; y < generator.seaLevel(); ++y) {
                if (area.block(x, y, z) != generator.waterBlock()) {
                    continue;
                }
                for (const auto &side : sides) {
                    const QcBlockPos next{x + side[0], y + side[1], z + side[2]};
                    if (area.block(next.x, next.y, next.z) == qcAirBlockId && !generator.isCave(next.x, next.y, next.z)) {
                        open.append(QStringLiteral("%1 %2 %3").arg(x).arg(y).arg(z));
                    }
                }
            }
        }
    }
    return open;
}

} // namespace QcWaterTestUtil
