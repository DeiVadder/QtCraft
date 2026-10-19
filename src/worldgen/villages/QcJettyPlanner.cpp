#include "worldgen/villages/QcJettyPlanner.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <cstdlib>

namespace {

// Whether the column is water from the terrain noise: its ground lies below the water surface (the
// block under the sea level), so at least one block of water stands on it.
bool isWater(const QcTerrainColumn &column, int seaLevel)
{
    return column.height - 1 < seaLevel - 1;
}

bool isWater(const QcTerrainGenerator &terrain, int x, int z)
{
    return isWater(terrain.column(x, z), terrain.seaLevel());
}

// The walkway follows the ground down to the shore in steps of at most this much.
constexpr int maxStep = 1;

// The ground past the road end down to the first water: the top block of every land cell before it
// (the water lies one block beyond the last). None when there is no water within reach or the ground
// before it rises or falls in steps the walkway cannot follow.
[[nodiscard]] std::optional<QList<int>> shoreHeights(const QcTerrainGenerator &terrain, const QcJettyPlanner::Request &request)
{
    QList<int> heights;
    int previous = request.deckY;
    for (int d = 1; d <= request.reach; ++d) {
        const int x = request.x + request.dx * d;
        const int z = request.z + request.dz * d;
        const QcTerrainColumn column = terrain.column(x, z);
        if (isWater(column, terrain.seaLevel())) {
            return heights;
        }
        if (std::abs(column.height - 1 - previous) > maxStep || column.overhang >= 1.0) {
            return std::nullopt;
        }
        previous = column.height - 1;
        heights.append(previous);
    }
    return std::nullopt;
}

} // namespace

namespace QcJettyPlanner {

std::optional<QList<QcVillagePlan::Deck>> lay(const QcTerrainGenerator &terrain, const Request &request)
{
    const std::optional<QList<int>> shore = shoreHeights(terrain, request);
    if (!shore) {
        return std::nullopt;
    }
    const QList<int> &heights = *shore;
    // Blocks past the road end to the first water.
    const int water = int(heights.size()) + 1;

    // Out over the water, but no further than it reaches (a narrow river ends the walkway early).
    int end = water;
    while (end < water + request.over - 1 && isWater(terrain, request.x + request.dx * (end + 1), request.z + request.dz * (end + 1))) {
        ++end;
    }
    if (end - water + 1 < 2) {
        return std::nullopt;
    }

    QList<QcVillagePlan::Deck> decks;
    const int half = request.width / 2;
    for (int d = 1; d <= end; ++d) {
        // Posts from the shore on, every few blocks and at the far end, under the edges.
        const bool postRow = d >= water - 1 && ((d - water + 1) % postSpacing == 0 || d == end);
        // On the ground's top block down to the shore, then just above the water.
        const int y = d < water ? heights[d - 1] : terrain.seaLevel();
        for (int w = -half; w <= half; ++w) {
            const int x = request.x + request.dx * d - request.dz * w;
            const int z = request.z + request.dz * d + request.dx * w;
            const bool post = postRow && (w == -half || w == half);
            decks.append({{x, y, z}, request.deck, post ? request.post : qcAirBlockId});
        }
    }
    return decks;
}

} // namespace QcJettyPlanner
