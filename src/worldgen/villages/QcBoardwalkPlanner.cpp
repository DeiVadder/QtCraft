#include "worldgen/villages/QcBoardwalkPlanner.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

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

} // namespace QcBoardwalkPlanner
