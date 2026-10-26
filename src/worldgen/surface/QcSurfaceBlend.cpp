#include "worldgen/surface/QcSurfaceBlend.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cmath>

namespace {

// Plants and snow on lowered ground are cleared this far above its old top.
constexpr int plantHeight = 3;

} // namespace

QcSurfaceBlend::QcSurfaceBlend(const QcTerrainGenerator &terrain)
    : m_terrain(terrain)
{
}

int QcSurfaceBlend::levelAt(int floor, int ground, double distance, int blend)
{
    const double freedom = slope * (blend + 1 - distance);
    if (freedom <= 0.0) {
        return ground;
    }
    return static_cast<int>(std::lround(std::clamp(static_cast<double>(floor), ground - freedom, ground + freedom)));
}

double QcSurfaceBlend::distanceTo(const QcStructurePlacement &placement, int x, int z)
{
    const QcBlockPos origin = placement.origin();
    const int dx = std::max({0, origin.x - x, x - (origin.x + placement.sizeX() - 1)});
    const int dz = std::max({0, origin.z - z, z - (origin.z + placement.sizeZ() - 1)});
    return std::hypot(static_cast<double>(dx), static_cast<double>(dz));
}

void QcSurfaceBlend::shape(QcChunk &chunk, const QcStructurePlacement &placement, int blend) const
{
    if (blend <= 0) {
        return;
    }
    // Limits: the ground is the terrain's height per column, so an overhang above it stays (the
    // planner keeps overhangs off the ring beside the walls, QcSurfacePlanner::levelsBeside), and a
    // cave the cut opens stays open, its liquid unsealed (caves rarely reach this close to the
    // surface; mine shafts seal theirs, QcMineshaftBuilder::seal).
    const int chunkX = chunk.pos().x * QcWorld::chunkSize;
    const int chunkZ = chunk.pos().z * QcWorld::chunkSize;
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            if (distanceTo(placement, chunkX + lx, chunkZ + lz) > blend) {
                continue;
            }
            const int ground = m_terrain.column(chunkX + lx, chunkZ + lz).height - 1;
            if (const std::optional<int> target = topAt(placement, blend, chunkX + lx, chunkZ + lz, ground)) {
                shapeColumn(chunk, {lx, ground, lz}, ground, *target);
            }
        }
    }
}

std::optional<int> QcSurfaceBlend::topAt(const QcStructurePlacement &placement, int blend, int x, int z, int ground) const
{
    const double distance = distanceTo(placement, x, z);
    // Under water the ground stays: no water wall and no dam.
    if (distance == 0.0 || distance > blend || ground < m_terrain.seaLevel()) {
        return std::nullopt;
    }
    // Never below the sea (a floor under water, `waterDepth`): no dry pit beside the water.
    const int floor = placement.origin().y + placement.structure().groundLayer;
    const int target = std::max(levelAt(floor, ground, distance, blend), m_terrain.seaLevel());
    return target == ground ? std::nullopt : std::optional<int>(target);
}

void QcSurfaceBlend::shapeColumn(QcChunk &chunk, QcBlockPos local, int ground, int target) const
{
    if (target <= 1 || std::max(ground, target) + plantHeight >= QcWorld::worldHeight) {
        return;
    }
    const int x = chunk.pos().x * QcWorld::chunkSize + local.x;
    const int z = chunk.pos().z * QcWorld::chunkSize + local.z;
    const QcBlockId surface = m_terrain.surfaceBlockAt(x, z);
    // Raised ground is the block under the old top (in a cave the surface block).
    const QcBlockId below = chunk.block(local.x, ground - 1, local.z);
    const QcBlockId filler = below == qcAirBlockId ? surface : below;
    // Above the new top air, so plants on removed ground go and those on raised ground do not float.
    for (int y = std::min(ground, target) + 1; y <= std::max(ground, target) + plantHeight; ++y) {
        chunk.setBlock(local.x, y, local.z, y < target ? filler : qcAirBlockId);
    }
    chunk.setBlock(local.x, target, local.z, surface);
}
