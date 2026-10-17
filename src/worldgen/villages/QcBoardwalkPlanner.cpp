#include "worldgen/villages/QcBoardwalkPlanner.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

// Over water, at least this share of the ground around the centre carries stilts.
constexpr double stiltShare = 0.6;

// The ground's top block of a column.
int groundOf(const QcTerrainGenerator &terrain, int x, int z)
{
    return terrain.column(x, z).height - 1;
}

// The facing of a stair whose higher side lies in direction (dx, dz).
QString facingTowards(int dx, int dz)
{
    if (dz < 0) {
        return u"north"_s;
    }
    if (dz > 0) {
        return u"south"_s;
    }
    return dx > 0 ? u"east"_s : u"west"_s;
}

} // namespace

namespace QcBoardwalkPlanner {

int floorY(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts)
{
    // The water surface is the block below the sea level.
    return terrain.seaLevel() - 1 + stilts.height;
}

bool carries(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, int x, int z)
{
    const QcTerrainColumn column = terrain.column(x, z);
    const int ground = column.height - 1;
    return column.overhang < 1.0 && ground <= floorY(terrain, stilts) && ground >= terrain.seaLevel() - 1 - stilts.maxDepth;
}

std::optional<int> floorFor(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, std::pair<int, int> low,
                            std::pair<int, int> high)
{
    const auto [x0, z0] = low;
    const auto [x1, z1] = high;
    const int mx = (x0 + x1) / 2;
    const int mz = (z0 + z1) / 2;
    const std::array<std::pair<int, int>, 9> samples{{{x0, z0}, {mx, z0}, {x1, z0}, {x0, mz}, {mx, mz}, {x1, mz}, {x0, z1}, {mx, z1}, {x1, z1}}};
    for (const auto &[x, z] : samples) {
        if (!carries(terrain, stilts, x, z)) {
            return std::nullopt;
        }
    }
    return floorY(terrain, stilts);
}

QcVillagePlan::Deck deck(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, std::pair<int, int> column,
                         QcBlockId block)
{
    const auto [x, z] = column;
    const bool onGrid = qcFloorMod(x, stilts.spacing) == 0 && qcFloorMod(z, stilts.spacing) == 0;
    return {{x, floorY(terrain, stilts), z}, block, onGrid ? stilts.post : qcAirBlockId};
}

QList<QcVillagePlan::Deck> stairs(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyle::Stilts &stilts,
                                  const End &end)
{
    const QcBlockId stair = content.withProperty(stilts.stair, u"facing"_s, facingTowards(-end.dx, -end.dz));
    const int floor = floorY(terrain, stilts);
    const int half = end.width / 2;
    QList<QcVillagePlan::Deck> cells;
    // The first stair lies a block below the floor; at most one stair per block of height.
    for (int d = 1; d <= stilts.height; ++d) {
        const int x = end.x + end.dx * d;
        const int z = end.z + end.dz * d;
        const int y = floor - d;
        const int ground = groundOf(terrain, x, z);
        if (ground < terrain.seaLevel() - 1) {
            // Water beyond: no way down to dry land here.
            return {};
        }
        if (ground >= y) {
            // The ground lies at most a step below the last stair (or the floor).
            break;
        }
        for (int w = -half; w <= half; ++w) {
            cells.append({{x - end.dz * w, y, z + end.dx * w}, stair, stilts.post});
        }
    }
    return cells;
}

std::optional<int> wetAround(const QcTerrainGenerator &terrain, const QcVillageStyle &style, std::pair<int, int> centre, int reach, int step)
{
    const auto [x, z] = centre;
    int samples = 0;
    int carried = 0;
    int wet = 0;
    for (int dz = -reach; dz <= reach; dz += step) {
        for (int dx = -reach; dx <= reach; dx += step) {
            ++samples;
            carried += QcBoardwalkPlanner::carries(terrain, style.stilts, x + dx, z + dz) ? 1 : 0;
            // Water stands over ground below the water surface.
            wet += terrain.column(x + dx, z + dz).height - 1 < terrain.seaLevel() - 1 ? 1 : 0;
        }
    }
    // Most of the ground carries stilts (pieces and boardwalks keep off the rest), and some of it
    // is water: a village over water stands in the wet part of its biome.
    const bool fits = double(carried) >= stiltShare * double(samples) && wet > 0 && double(wet) >= style.stilts.water * double(samples);
    return fits ? std::optional<int>(wet) : std::nullopt;
}

void layStairs(const QcTerrainGenerator &terrain, const QcContentRegistry &content, QcVillageDraft &draft, const QcVillageDraft::Road &road)
{
    const int endX = road.x + road.dx * (road.length - 1);
    const int endZ = road.z + road.dz * (road.length - 1);
    const QList<QcVillagePlan::Deck> flight =
        stairs(terrain, content, draft.style->stilts, {endX, endZ, road.dx, road.dz, 2 * QcVillageDraft::roadHalfWidth + 1});
    if (flight.isEmpty()) {
        return;
    }

    QcVillageDraft::Rect rect{flight.front().at.x, flight.front().at.z, flight.front().at.x, flight.front().at.z};
    for (const QcVillagePlan::Deck &stair : flight) {
        rect = {std::min(rect.x0, stair.at.x), std::min(rect.z0, stair.at.z), std::max(rect.x1, stair.at.x), std::max(rect.z1, stair.at.z)};
    }
    const int spacing = draft.region.spacing;
    const int regionX0 = draft.region.x * spacing;
    const int regionZ0 = draft.region.z * spacing;
    const bool inRegion = rect.x0 >= regionX0 && rect.z0 >= regionZ0 && rect.x1 < regionX0 + spacing && rect.z1 < regionZ0 + spacing;
    const bool hitsPiece = std::ranges::any_of(draft.pieces, [&](QcVillageDraft::Rect other) { return rect.intersects(other, 0); });
    if (!inRegion || hitsPiece || draft.touchesRoad(rect, 0)) {
        return;
    }
    draft.plan.decks.append(flight);
    draft.markRoad(rect);
    draft.stairs.append(rect);

    // The land access stays dry: the stairs and a strip beyond them through the wetland's reach.
    const int beyond = draft.style->wetland.reach() + 1;
    const int farX = endX + road.dx * (int(flight.size()) / (2 * QcVillageDraft::roadHalfWidth + 1) + beyond);
    const int farZ = endZ + road.dz * (int(flight.size()) / (2 * QcVillageDraft::roadHalfWidth + 1) + beyond);
    const int side = QcVillageDraft::roadHalfWidth + 1;
    draft.plan.wetland.dry.append({std::min(rect.x0, farX - side * std::abs(road.dz)), std::min(rect.z0, farZ - side * std::abs(road.dx)),
                                    std::max(rect.x1, farX + side * std::abs(road.dz)), std::max(rect.z1, farZ + side * std::abs(road.dx))});
}

void planWetland(QcVillageDraft &draft)
{
    // The wetland spreads from the pieces and boardwalks, not from the stairs down to land.
    QcVillagePlan::Wetland &wetland = draft.plan.wetland;
    wetland.settings = draft.style->wetland;
    const QList<QcVillageDraft::Rect> covered = draft.pieces + draft.roads;
    for (const QcVillageDraft::Rect rect : covered) {
        if (!draft.stairs.contains(rect)) {
            wetland.wet.append({rect.x0, rect.z0, rect.x1, rect.z1});
        }
    }

    // Its columns are part of the village: no trees or plants grow there (decision E-403).
    // The village stays inside its region, so its chunks need no other plan.
    const int reach = wetland.settings.reach();
    const int regionX0 = draft.region.x * draft.region.spacing;
    const int regionZ0 = draft.region.z * draft.region.spacing;
    QcVillagePlan &plan = draft.plan;
    plan.minX = std::max(regionX0, plan.minX - reach);
    plan.minZ = std::max(regionZ0, plan.minZ - reach);
    plan.maxX = std::min(regionX0 + draft.region.spacing - 1, plan.maxX + reach);
    plan.maxZ = std::min(regionZ0 + draft.region.spacing - 1, plan.maxZ + reach);
}

} // namespace QcBoardwalkPlanner
