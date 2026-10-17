#include "worldgen/villages/QcWetlandShaper.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Size of the bends of the wetland's edge, in blocks.
constexpr double frayScale = 9.0;
// Plants and snow on lowered ground are cleared this far above its old top.
constexpr int plantHeight = 3;

} // namespace

QcWetlandShaper::QcWetlandShaper(const QcTerrainGenerator &terrain, quint64 seed)
    : m_terrain(terrain)
    , m_fray(qcMixSeed(seed, qcNameSalt(u"qtcraft:village_wetland"_s)))
{
}

double QcWetlandShaper::distanceToWet(const QcVillagePlan::Wetland &wetland, int x, int z)
{
    double nearest = 1e9;
    for (const QcVillagePlan::Area &area : wetland.wet) {
        const int dx = std::max({0, area.x0 - x, x - area.x1});
        const int dz = std::max({0, area.z0 - z, z - area.z1});
        nearest = std::min(nearest, std::hypot(double(dx), double(dz)));
        if (nearest == 0.0) {
            break;
        }
    }
    return nearest;
}

std::optional<int> QcWetlandShaper::groundAt(const QcVillagePlan &plan, int x, int z) const
{
    if (plan.wetland.settings.bottom == qcAirBlockId || !plan.covers(x, z, 0)) {
        return std::nullopt;
    }
    return targetAt(plan, x, z, m_terrain.column(x, z).height - 1);
}

std::optional<int> QcWetlandShaper::targetAt(const QcVillagePlan &plan, int x, int z, int ground) const
{
    const QcVillagePlan::Wetland &wetland = plan.wetland;
    const QcVillageStyle::Wetland &settings = wetland.settings;
    if (std::ranges::any_of(wetland.dry, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); })) {
        return std::nullopt;
    }

    const double distance = distanceToWet(wetland, x, z);
    if (distance > settings.reach()) {
        return std::nullopt;
    }
    // The edge bends with the noise between the margin and the margin plus the fray.
    const double fray = std::clamp(0.5 + 0.7 * m_fray.fractal2(x / frayScale, z / frayScale, 2), 0.0, 1.0);
    const double edge = settings.margin + settings.fray * fray;
    if (distance > edge + settings.blend) {
        return std::nullopt;
    }

    // Inside the edge the bottom lies `depth` below the water surface; beyond it the ground rises
    // (or sinks) back to the terrain over the blend width.
    const int waterSurface = m_terrain.seaLevel() - 1;
    const int bottom = waterSurface - settings.depth;
    const double share = std::clamp((distance - edge) / settings.blend, 0.0, 1.0);
    const int target = int(std::lround(bottom + share * (ground - bottom)));
    if (target == ground && ground > waterSurface) {
        return std::nullopt;
    }
    return target;
}

void QcWetlandShaper::shape(QcChunk &chunk, const QcVillagePlan &plan) const
{
    const QcBlockId bottom = plan.wetland.settings.bottom;
    if (bottom == qcAirBlockId || !plan.overlaps(chunk.pos(), 0)) {
        return;
    }

    const int chunkX = chunk.pos().x * QcWorld::chunkSize;
    const int chunkZ = chunk.pos().z * QcWorld::chunkSize;
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int x = chunkX + lx;
            const int z = chunkZ + lz;
            if (!plan.covers(x, z, 0)) {
                continue;
            }
            const int ground = m_terrain.column(x, z).height - 1;
            if (const std::optional<int> target = targetAt(plan, x, z, ground)) {
                shapeColumn(chunk, {lx, ground, lz}, *target, bottom);
            }
        }
    }
}

void QcWetlandShaper::shapeColumn(QcChunk &chunk, QcBlockPos local, int target, QcBlockId bottom) const
{
    const int x = chunk.pos().x * QcWorld::chunkSize + local.x;
    const int z = chunk.pos().z * QcWorld::chunkSize + local.z;
    const int ground = local.y;
    const int waterSurface = m_terrain.seaLevel() - 1;
    if (target <= 0 || std::max(ground, waterSurface) + plantHeight >= QcWorld::worldHeight) {
        return;
    }

    // Removed ground: water up to the water surface, air above, plants on it gone.
    for (int y = target + 1; y <= std::max(ground, waterSurface) + plantHeight; ++y) {
        const bool wet = y <= waterSurface;
        if (y <= std::max(ground, waterSurface) || chunk.block(local.x, y, local.z) != qcAirBlockId) {
            chunk.setBlock(local.x, y, local.z, wet ? m_terrain.waterBlock() : qcAirBlockId);
        }
    }
    // Raised ground (deep water filled up) is bottom.
    for (int y = ground + 1; y < target; ++y) {
        chunk.setBlock(local.x, y, local.z, bottom);
    }
    chunk.setBlock(local.x, target, local.z, target <= waterSurface ? bottom : m_terrain.surfaceBlockAt(x, z));
}
