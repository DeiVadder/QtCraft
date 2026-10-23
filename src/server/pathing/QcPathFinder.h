#pragma once

#include "server/pathing/QcPathWindow.h"

#include <array>
#include <optional>
#include <vector>

// What one search looks for (decision E-400).
struct QcPathRequest
{
    // Feet cell of the creature and of its goal.
    QcBlockPos start;
    QcBlockPos goal;
    // Cells around the goal (horizontally, and one up or down) that count as there: a hunter only
    // needs to come next to its prey.
    int closeEnough = 0;
    // Nodes the search may expand at most, and waypoints the path keeps at most.
    int maxNodes = 512;
    int maxLength = 64;
};

// A path as feet cells after the start, in order.
struct QcPathResult
{
    std::vector<QcBlockPos> cells;
    // The creature stood somewhere a search can start from.
    bool started = false;
    // The last cell is at the goal; otherwise the path leads to the reached cell closest to it
    // (a partial path: the goal is out of reach, behind the node budget or beyond `maxLength`).
    bool reachesGoal = false;
    // Nodes expanded (counted against the budgets).
    int nodes = 0;
};

// A* over the cells a creature can stand in (QcPathTerrain), bounded by a node budget and by the
// window around the start (QcPathWindow). Moves: the four sides (a step up of at most
// QcPathRules::climbHeight with room to jump, level, or a drop of at most QcPathRules::maxDrop,
// deeper only into deep water), up and down along a ladder (a creature that climbs), and the diagonals between two free sides
// on the same level. Costs:
// distance, a little for climbing and dropping, much for water (a walker), magma floors and cobwebs,
// a little beside walls, so paths keep to open cells, and more beside fire and lava. The
// heuristic is weighted (1.25): paths are at most a quarter longer than the shortest, for far fewer
// nodes.
class QcPathFinder
{
public:
    static constexpr float heuristicWeight = 1.25f;

    QcPathResult find(const QcBlockAccess &world, const QcPathCellTable &table, QcPathRules rules,
                      const QcPathRequest &request);

private:
    struct Move
    {
        QcBlockPos to;
        float cost = 0.0f;
    };
    // The moves from one node: at most four sides, two climbs and four diagonals.
    class Moves
    {
    public:
        void add(Move move) { m_moves[static_cast<std::size_t>(m_count++)] = move; }
        // Adds `cost` to every move.
        void raise(float cost)
        {
            for (Move &move : *this) {
                move.cost += cost;
            }
        }
        Move *begin() { return m_moves.data(); }
        Move *end() { return m_moves.data() + m_count; }
        const Move *begin() const { return m_moves.data(); }
        const Move *end() const { return m_moves.data() + m_count; }

    private:
        std::array<Move, 10> m_moves{};
        int m_count = 0;
    };

    // Where a creature at `feet` (falling, in deep water) can start from: its cell, a few below or
    // two above.
    std::optional<QcBlockPos> startCell(QcBlockPos feet, QcPathRules rules);
    // The moves from a node.
    [[nodiscard]] Moves expand(QcBlockPos at, QcPathRules rules);
    std::optional<Move> sideStep(QcBlockPos at, QcPathStand here, QcBlockPos side, QcPathRules rules);
    // Up and down a ladder the feet are on: to the ladder cell above, to the cell below (Moves is an
    // inline buffer, returning it allocates nothing).
    [[nodiscard]] Moves climbs(QcBlockPos at, QcPathRules rules);
    // Fire or lava in the side cell or below it (a side that cannot be entered).
    bool dangerous(QcBlockPos side);
    // The best of the visited nodes (closest to the goal) and the cells from the start to it.
    QcPathResult pathTo(int end, int startIndex, const QcPathRequest &request);

    QcPathWindow m_window;
    // Open nodes as (estimated total cost, window index); a min-heap with stale entries skipped.
    std::vector<std::pair<float, int>> m_heap;
};
