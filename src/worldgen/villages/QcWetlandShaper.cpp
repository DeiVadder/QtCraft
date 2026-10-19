#include "worldgen/villages/QcWetlandShaper.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Size of the bends of the wetland's edge, in blocks.
constexpr double frayScale = 9.0;
// How far the wetland's ground may differ from the terrain per block of distance inward from the
// blend's outer end: below one block, so with the bends of the edge two neighbouring columns differ
// by at most a block more than the terrain (decision E-660).
constexpr double slope = 0.6;
// Plants and snow on lowered ground are cleared this far above its old top.
constexpr int plantHeight = 3;

} // namespace

QcWetlandShaper::QcWetlandShaper(const QcTerrainGenerator &terrain, quint64 seed)
    : m_terrain(terrain)
    , m_fray(qcMixSeed(seed, qcNameSalt(u"qtcraft:village_wetland"_s)))
    , m_pattern(seed)
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
    const std::optional<Column> column = columnAt(plan, x, z);
    return column ? std::optional<int>(column->top) : std::nullopt;
}

std::optional<QcWetlandShaper::Column> QcWetlandShaper::columnAt(const QcVillagePlan &plan, int x, int z) const
{
    if (plan.wetland.settings.bottom == qcAirBlockId || !plan.covers(x, z, 0)) {
        return std::nullopt;
    }
    return targetAt(plan, x, z, m_terrain.column(x, z).height - 1);
}

int QcWetlandShaper::levelAt(const QcVillagePlan &plan, int x, int z, int ground) const
{
    const QcVillagePlan::Wetland &wetland = plan.wetland;
    const QcVillageStyle::Wetland &settings = wetland.settings;
    if (!plan.covers(x, z, 0) || std::ranges::any_of(wetland.dry, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); })) {
        return ground;
    }

    const double distance = distanceToWet(wetland, x, z);
    if (distance > settings.reach()) {
        return ground;
    }
    // The edge bends with the noise between the margin and the margin plus the fray. The ground
    // may move away from the terrain by `slope` blocks per block inward from the blend's outer end,
    // so the wetland reaches the water surface where the terrain lies near it and only cuts a
    // gentle slope into ground far above (decision E-660).
    const double fray = std::clamp(0.5 + 0.7 * m_fray.fractal2(x / frayScale, z / frayScale, 2), 0.0, 1.0);
    const double freedom = slope * (settings.margin + settings.fray * fray + settings.blend - distance);
    if (freedom <= 0.0) {
        return ground;
    }
    const int waterSurface = m_terrain.seaLevel() - 1;
    return int(std::lround(std::clamp(double(waterSurface), ground - freedom, ground + freedom)));
}

bool QcWetlandShaper::poolFits(const QcVillagePlan &plan, int x, int z) const
{
    // Every side at most at the water surface, so the pool's edge is no higher than its depth.
    const int waterSurface = m_terrain.seaLevel() - 1;
    return std::ranges::all_of(std::array{std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}, [&](auto side) {
        const int nx = x + side.first;
        const int nz = z + side.second;
        return levelAt(plan, nx, nz, m_terrain.column(nx, nz).height - 1) <= waterSurface;
    });
}

std::optional<QcWetlandShaper::Column> QcWetlandShaper::targetAt(const QcVillagePlan &plan, int x, int z, int ground) const
{
    const QcVillagePlan::Wetland &wetland = plan.wetland;
    if (std::ranges::any_of(wetland.dry, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); })
        || distanceToWet(wetland, x, z) > wetland.settings.reach()) {
        return std::nullopt;
    }
    const int level = levelAt(plan, x, z, ground);
    const int waterSurface = m_terrain.seaLevel() - 1;
    if (level > waterSurface) {
        return level == ground ? std::nullopt : std::optional<Column>(Column{level, QcWetlandPattern::Cover::Grass});
    }
    if (level < waterSurface) {
        return Column{level, QcWetlandPattern::Cover::Mud};
    }

    // The flat wetland at the water surface: pools cut into it where its sides are flat too.
    QcWetlandPattern::Cover cover = m_pattern.coverAt(wetland, x, z);
    const bool pool = cover == QcWetlandPattern::Cover::Water || cover == QcWetlandPattern::Cover::DeepWater;
    if (pool && !poolFits(plan, x, z)) {
        cover = QcWetlandPattern::Cover::Mud;
    }
    switch (cover) {
    case QcWetlandPattern::Cover::DeepWater:
        return Column{waterSurface - wetland.settings.depth - 1, cover};
    case QcWetlandPattern::Cover::Water:
        return Column{waterSurface - wetland.settings.depth, cover};
    case QcWetlandPattern::Cover::Mud:
    case QcWetlandPattern::Cover::Grass:
        break;
    }
    return Column{waterSurface, cover};
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
            if (const std::optional<Column> target = targetAt(plan, x, z, ground)) {
                shapeColumn(chunk, {lx, ground, lz}, *target, bottom);
                plant(chunk, {lx, target->top, lz}, plan);
            }
        }
    }
}

void QcWetlandShaper::shapeColumn(QcChunk &chunk, QcBlockPos local, Column column, QcBlockId bottom) const
{
    const int x = chunk.pos().x * QcWorld::chunkSize + local.x;
    const int z = chunk.pos().z * QcWorld::chunkSize + local.z;
    const int ground = local.y;
    const int target = column.top;
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
    const bool grass = target >= waterSurface && column.cover == QcWetlandPattern::Cover::Grass;
    chunk.setBlock(local.x, target, local.z, grass ? m_terrain.surfaceBlockAt(x, z) : bottom);
}

void QcWetlandShaper::plant(QcChunk &chunk, QcBlockPos local, const QcVillagePlan &plan) const
{
    const QcVillageStyle::Wetland &settings = plan.wetland.settings;
    const int x = chunk.pos().x * QcWorld::chunkSize + local.x;
    const int z = chunk.pos().z * QcWorld::chunkSize + local.z;
    const int waterSurface = m_terrain.seaLevel() - 1;
    if (local.y != waterSurface || !m_pattern.plantAt(settings, x, z)) {
        return;
    }

    // Beside a pool (its water at the plant's ground level), never on pieces or boardwalks.
    if (std::ranges::any_of(plan.wetland.wet, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); })) {
        return;
    }
    const bool besidePool = std::ranges::any_of(std::array{std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}, [&](auto side) {
        const std::optional<int> top = groundAt(plan, x + side.first, z + side.second);
        return top && *top < waterSurface;
    });
    if (!besidePool) {
        return;
    }
    // One or two blocks tall, it grows on by itself.
    const int height = 1 + ((x ^ z) & 1);
    for (int y = waterSurface + 1; y <= waterSurface + height; ++y) {
        chunk.setBlock(local.x, y, local.z, settings.plant);
    }
}
