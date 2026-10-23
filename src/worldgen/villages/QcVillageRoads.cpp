#include "worldgen/villages/QcVillageRoads.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <tuple>

namespace QcVillageRoads {

const QcVillageStyle::Road &choose(const QList<QcVillageStyle::Road> &roads, double random)
{
    int total = 0;
    for (const QcVillageStyle::Road &road : roads) {
        total += road.weight;
    }
    int pick = static_cast<int>(random * total);
    for (const QcVillageStyle::Road &road : roads) {
        if (pick < road.weight) {
            return road;
        }
        pick -= road.weight;
    }
    return roads.back();
}

QList<QcVillagePlan::Step> steps(const QcVillagePlan &plan, QcBlockId step, QcBlockId branchStep)
{
    // Column -> the road's top block height and its step block.
    struct Cell
    {
        int y = 0;
        QcBlockId step = qcAirBlockId;
    };
    QHash<std::pair<int, int>, Cell> cells;
    for (const QcBlockPos &pos : plan.branchRoads) {
        cells.insert({pos.x, pos.z}, {pos.y, branchStep});
    }
    for (const QcBlockPos &pos : plan.roads) {
        cells.insert({pos.x, pos.z}, {pos.y, step});
    }

    QList<QcVillagePlan::Step> result;
    constexpr std::array<std::pair<int, int>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (auto it = cells.cbegin(); it != cells.cend(); ++it) {
        if (it->step == qcAirBlockId) {
            continue;
        }
        const auto [x, z] = it.key();
        const bool below = std::ranges::any_of(sides, [&](std::pair<int, int> side) {
            const auto neighbour = cells.constFind({x + side.first, z + side.second});
            return neighbour != cells.cend() && neighbour->y == it->y + 1;
        });
        if (below) {
            result.append({{x, it->y + 1, z}, it->step});
        }
    }
    // The hash's order is no function of the content; the plan's is.
    std::ranges::sort(result, {}, [](QcVillagePlan::Step s) { return std::tuple{s.at.x, s.at.z}; });
    return result;
}

} // namespace QcVillageRoads
