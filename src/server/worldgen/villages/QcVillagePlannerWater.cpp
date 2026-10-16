#include "QcVillagePlanner.h"

#include "QcBoardwalkPlanner.h"
#include "QcTerrainGenerator.h"

#include <algorithm>

// The parts of the village planner for styles over water (decision E-407): where a centre is wet
// enough, the stairs from the boardwalks down to land and the wetland's areas.

std::optional<int> QcVillagePlanner::wetAround(const QcVillageStyle &style, int x, int z) const
{
    const int reach = m_settings.radius / 2 / flatnessStep * flatnessStep;
    int samples = 0;
    int carried = 0;
    int wet = 0;
    for (int dz = -reach; dz <= reach; dz += flatnessStep) {
        for (int dx = -reach; dx <= reach; dx += flatnessStep) {
            ++samples;
            carried += QcBoardwalkPlanner::carries(m_terrain, style.stilts, x + dx, z + dz) ? 1 : 0;
            // Water stands over ground below the water surface.
            wet += m_terrain.column(x + dx, z + dz).height - 1 < m_terrain.seaLevel() - 1 ? 1 : 0;
        }
    }
    // Most of the ground carries stilts (pieces and boardwalks keep off the rest), and some of it
    // is water: a village over water stands in the wet part of its biome.
    const bool fits = double(carried) >= stiltShare * double(samples) && wet > 0 && double(wet) >= style.stilts.water * double(samples);
    return fits ? std::optional<int>(wet) : std::nullopt;
}

void QcVillagePlanner::planWetland(Layout &layout) const
{
    // The wetland spreads from the pieces and boardwalks, not from the stairs down to land.
    QcVillagePlan::Wetland &wetland = layout.plan.wetland;
    wetland.settings = layout.style->wetland;
    const QList<Rect> covered = layout.pieces + layout.roads;
    for (const Rect rect : covered) {
        if (!layout.stairs.contains(rect)) {
            wetland.wet.append({rect.x0, rect.z0, rect.x1, rect.z1});
        }
    }

    // Its columns are part of the village: no trees or plants grow there (decision E-403).
    // The village stays inside its region, so its chunks need no other plan.
    const int reach = wetland.settings.reach();
    const int regionX0 = layout.regionX * m_spacing;
    const int regionZ0 = layout.regionZ * m_spacing;
    QcVillagePlan &plan = layout.plan;
    plan.minX = std::max(regionX0, plan.minX - reach);
    plan.minZ = std::max(regionZ0, plan.minZ - reach);
    plan.maxX = std::min(regionX0 + m_spacing - 1, plan.maxX + reach);
    plan.maxZ = std::min(regionZ0 + m_spacing - 1, plan.maxZ + reach);
}

void QcVillagePlanner::layStairs(Layout &layout, const Road &road) const
{
    const int endX = road.x + road.dx * (road.length - 1);
    const int endZ = road.z + road.dz * (road.length - 1);
    const QList<QcVillagePlan::Deck> stairs =
        QcBoardwalkPlanner::stairs(m_terrain, m_content, layout.style->stilts, {endX, endZ, road.dx, road.dz, 2 * roadHalfWidth + 1});
    if (stairs.isEmpty()) {
        return;
    }

    Rect rect{stairs.front().at.x, stairs.front().at.z, stairs.front().at.x, stairs.front().at.z};
    for (const QcVillagePlan::Deck &stair : stairs) {
        rect = {std::min(rect.x0, stair.at.x), std::min(rect.z0, stair.at.z), std::max(rect.x1, stair.at.x), std::max(rect.z1, stair.at.z)};
    }
    const int regionX0 = layout.regionX * m_spacing;
    const int regionZ0 = layout.regionZ * m_spacing;
    const bool inRegion = rect.x0 >= regionX0 && rect.z0 >= regionZ0 && rect.x1 < regionX0 + m_spacing && rect.z1 < regionZ0 + m_spacing;
    const bool hitsPiece = std::ranges::any_of(layout.pieces, [&](Rect other) { return rect.intersects(other, 0); });
    if (!inRegion || hitsPiece || touchesRoad(layout, rect, 0)) {
        return;
    }
    layout.plan.decks.append(stairs);
    markRoad(layout, rect);
    layout.stairs.append(rect);

    // The land access stays dry: the stairs and a strip beyond them through the wetland's reach.
    const int beyond = layout.style->wetland.reach() + 1;
    const int farX = endX + road.dx * (int(stairs.size()) / (2 * roadHalfWidth + 1) + beyond);
    const int farZ = endZ + road.dz * (int(stairs.size()) / (2 * roadHalfWidth + 1) + beyond);
    const int side = roadHalfWidth + 1;
    layout.plan.wetland.dry.append({std::min(rect.x0, farX - side * std::abs(road.dz)), std::min(rect.z0, farZ - side * std::abs(road.dx)),
                                    std::max(rect.x1, farX + side * std::abs(road.dz)), std::max(rect.z1, farZ + side * std::abs(road.dx))});
}
