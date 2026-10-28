#include "worldgen/generators/QcBiomeMap.h"

#include "common/core/QcIntMath.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"
#include "worldgen/towers/QcCoastalTowerPlan.h"
#include "worldgen/villages/QcVillageGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

using namespace Qt::StringLiterals;

namespace {

// The regions of edge `spacing` blocks that a map's area reaches into.
struct Regions
{
    int x0 = 0;
    int z0 = 0;
    int x1 = 0;
    int z1 = 0;
};

Regions regionsOver(QcBiomeMap::Area area, int spacing)
{
    return {qcFloorDiv(area.centreX - area.radius, spacing), qcFloorDiv(area.centreZ - area.radius, spacing),
            qcFloorDiv(area.centreX + area.radius - 1, spacing), qcFloorDiv(area.centreZ + area.radius - 1, spacing)};
}

bool inside(QcBiomeMap::Area area, int x, int z)
{
    return x >= area.centreX - area.radius && x < area.centreX + area.radius && z >= area.centreZ - area.radius &&
           z < area.centreZ + area.radius;
}

// The place of each region's plan in `area` that `placeOf` finds; none when cancelled first.
template<typename PlaceOf>
[[nodiscard]] std::optional<QList<QcBiomeMap::Site>> sitesInRegions(QcBiomeMap::Area area, int spacing, const QcBiomeMap::Cancelled &cancelled,
                                                                    const PlaceOf &placeOf)
{
    QList<QcBiomeMap::Site> sites;
    const Regions regions = regionsOver(area, spacing);
    for (int rz = regions.z0; rz <= regions.z1; ++rz) {
        for (int rx = regions.x0; rx <= regions.x1; ++rx) {
            if (cancelled && cancelled()) {
                return std::nullopt;
            }
            const std::optional<QcBiomeMap::Site> site = placeOf(rx, rz);
            if (site && inside(area, site->x, site->z)) {
                sites.append(*site);
            }
        }
    }
    return sites;
}

} // namespace

QcBiomeMap::QcBiomeMap(Area area)
    : m_area(area)
{
    m_cells.resize(static_cast<qsizetype>(size()) * size());
}

QcBiomeMap::Cell QcBiomeMap::cellAt(const QcTerrainGenerator &generator, int x, int z)
{
    const QcTerrainGenerator::Column column = generator.column(x, z);
    // A river channel holds water; any other column below the sea level is filled with water up
    // to it (QcTerrainGenerator::fillColumn).
    const Water water = generator.isRiverBiome(column.biome) ? Water::River
                        : column.height < generator.seaLevel() ? Water::Sea
                                                               : Water::Land;
    // Every column under the sea level gets the top layer of its water from frozenWaterAt
    // (QcTerrainGenerator::fillColumn).
    const bool frozen = column.height < generator.seaLevel() && generator.frozenWaterAt(x, z, column) != generator.waterBlock();
    return {column.biome, column.height, water, frozen};
}

std::optional<QcBiomeMap> QcBiomeMap::sample(const QcTerrainGenerator &generator, Area area, const Cancelled &cancelled)
{
    QcBiomeMap map(area);
    const int size = map.size();
    for (int z = 0; z < size; ++z) {
        if (cancelled && cancelled()) {
            return std::nullopt;
        }
        for (int x = 0; x < size; ++x) {
            map.m_cells[static_cast<qsizetype>(z) * size + x] = cellAt(generator, area.blockX(x), area.blockZ(z));
        }
    }
    return map;
}

QList<int> QcBiomeMap::biomeCells(int biomeCount) const
{
    QList<int> counts(biomeCount, 0);
    for (const Cell &cell : m_cells) {
        if (cell.water != Water::Sea && cell.biome >= 0 && cell.biome < biomeCount) {
            ++counts[cell.biome];
        }
    }
    return counts;
}

std::optional<QList<QcBiomeMap::Site>> QcBiomeMap::sites(const QcTerrainGenerator &generator, Area area, const Cancelled &cancelled)
{
    QList<Site> sites;
    const QcVillageGenerator &villages = generator.villages();
    if (villages.spacing() > 0) {
        const std::optional<QList<Site>> found = sitesInRegions(area, villages.spacing(), cancelled, [&villages](int rx, int rz) -> std::optional<Site> {
            const std::shared_ptr<const QcVillagePlan> plan = villages.regionPlan(rx, rz);
            return plan ? std::optional(Site{Site::Kind::Village, plan->centre.x, plan->centre.z, u"village"_s}) : std::nullopt;
        });
        if (!found) {
            return std::nullopt;
        }
        sites.append(*found);
    }

    const QcSurfaceStructures &surface = generator.surfaceStructures();
    for (qsizetype kind = 0; kind < surface.planner().kinds().size(); ++kind) {
        const QcSurfacePlanner::Kind &type = surface.planner().kinds().at(kind);
        const std::optional<QList<Site>> found = sitesInRegions(area, type.rule.spacing, cancelled, [&](int rx, int rz) -> std::optional<Site> {
            const std::shared_ptr<const QcStructurePlacement> plan = surface.planOf(static_cast<int>(kind), rx, rz);
            if (!plan) {
                return std::nullopt;
            }
            const QcSurfacePlanner::Rect footprint = QcSurfacePlanner::footprintOf(*plan);
            return Site{Site::Kind::Structure, footprint.middleX(), footprint.middleZ(), type.name};
        });
        if (!found) {
            return std::nullopt;
        }
        sites.append(*found);
    }

    const QcCoastalTowerGenerator &towers = generator.coastalTowers();
    if (towers.planner().enabled()) {
        const int spacing = towers.planner().settings().regionSize;
        const std::optional<QList<Site>> found = sitesInRegions(area, spacing, cancelled, [&towers](int rx, int rz) -> std::optional<Site> {
            const std::shared_ptr<const QcCoastalTowerPlan> plan = towers.planOf(rx, rz);
            return plan ? std::optional(Site{Site::Kind::Structure, plan->doorstep.x, plan->doorstep.z, u"coastal_tower"_s}) : std::nullopt;
        });
        if (!found) {
            return std::nullopt;
        }
        sites.append(*found);
    }

    return sites;
}

QStringList QcBiomeMap::siteKinds(const QcTerrainGenerator &generator)
{
    QStringList kinds{u"village"_s, u"coastal_tower"_s};
    for (const QcSurfacePlanner::Kind &kind : generator.surfaceStructures().planner().kinds()) {
        kinds.append(kind.name);
    }
    return kinds;
}
