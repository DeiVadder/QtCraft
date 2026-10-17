#include "server/pathing/QcPathFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <utility>

namespace {

constexpr float diagonalCost = std::numbers::sqrt2_v<float>;
constexpr float climbCost = 0.5f;
constexpr float dropCostPerBlock = 0.4f;
// A walker swims only without a dry way of about this many cells more.
constexpr float waterCost = 8.0f;
constexpr float hurtingFloorCost = 12.0f;
constexpr float slowCellCost = 4.0f;
// Per side of a cell that cannot be entered: paths keep off walls and edges where they can.
constexpr float wallCost = 0.25f;
// Per side of a cell with fire or lava beside or below it: a body that strays hurts itself.
constexpr float dangerBesideCost = 2.0f;
// Neighbours whose standing heights differ by at most this are on the same level for a diagonal.
constexpr double sameLevel = 0.25;
// How far below and above its feet a creature may be when a search starts (falling, swimming).
constexpr int startBelow = 3;
constexpr int startAbove = 2;

constexpr std::array<std::array<int, 2>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

float standCost(QcPathStand stand, QcPathRules rules)
{
    float cost = 0.0f;
    if (stand.water && !rules.swims) {
        cost += waterCost;
    }
    if (stand.hurts) {
        cost += hurtingFloorCost;
    }
    if (stand.slows) {
        cost += slowCellCost;
    }
    return cost;
}

// Octile distance horizontally plus half a step per block of height.
float estimate(QcBlockPos from, QcBlockPos to)
{
    const int dx = std::abs(from.x - to.x);
    const int dz = std::abs(from.z - to.z);
    const int dy = std::abs(from.y - to.y);
    return float(std::max(dx, dz)) + (diagonalCost - 1.0f) * float(std::min(dx, dz)) + 0.5f * float(dy);
}

bool sameLevelAs(QcPathStand a, QcPathStand b)
{
    return std::abs(a.lift() - b.lift()) <= sameLevel;
}

bool isThere(QcBlockPos at, const QcPathRequest &request)
{
    return std::abs(at.x - request.goal.x) <= request.closeEnough && std::abs(at.z - request.goal.z) <= request.closeEnough
           && std::abs(at.y - request.goal.y) <= 1;
}

} // namespace

std::optional<QcBlockPos> QcPathFinder::startCell(QcBlockPos feet, QcPathRules rules)
{
    for (int down = 0; down <= startBelow; ++down) {
        if (QcPathTerrain::standAt(m_window, feet.offset(0, -down, 0), rules).ok) {
            return feet.offset(0, -down, 0);
        }
    }
    for (int up = 1; up <= startAbove; ++up) {
        if (QcPathTerrain::standAt(m_window, feet.offset(0, up, 0), rules).ok) {
            return feet.offset(0, up, 0);
        }
    }
    return std::nullopt;
}

std::optional<QcPathFinder::Move> QcPathFinder::sideStep(QcBlockPos at, QcPathStand here, QcBlockPos side,
                                                         QcPathRules rules)
{
    const double level = at.y + here.lift();

    // Level, or half a block up or down (slabs); a ladder's cell beside is entered at its foot.
    const QcPathStand beside = QcPathTerrain::standAt(m_window, side, rules);
    if (beside.ok) {
        const double rise = side.y + beside.lift() - level;
        if (rise > 0.0 && !QcPathTerrain::clearAbove(m_window, at, rise + here.lift(), rules)) {
            return std::nullopt;
        }
        return Move{side, 1.0f + standCost(beside, rules)};
    }

    // Up a block: only with room above the current cell for the jump.
    const QcPathStand above = QcPathTerrain::standAt(m_window, side.offset(0, 1, 0), rules);
    if (above.ok) {
        const double rise = side.y + 1 + above.lift() - level;
        if (rise > QcPathRules::climbHeight + 1e-6 || !QcPathTerrain::clearAbove(m_window, at, rise + here.lift(), rules)) {
            return std::nullopt;
        }
        return Move{side.offset(0, 1, 0), 1.0f + climbCost + standCost(above, rules)};
    }

    // Down: the body passes the edge at its height, then falls to the first floor below (deeper
    // than QcPathRules::maxDrop only into deep water).
    if (!QcPathTerrain::passable(m_window, side, rules, true) || !QcPathTerrain::clearAbove(m_window, side, here.lift(), rules)) {
        return std::nullopt;
    }
    for (int drop = 1; drop <= std::max(rules.maxDrop, QcPathRules::waterDrop); ++drop) {
        const QcBlockPos landing = side.offset(0, -drop, 0);
        const QcPathStand stand = QcPathTerrain::standAt(m_window, landing, rules);
        if (stand.ok) {
            if (!QcPathTerrain::safeLanding(m_window, landing, drop, rules)) {
                return std::nullopt;
            }
            return Move{landing, 1.0f + dropCostPerBlock * float(drop) + standCost(stand, rules)};
        }
        if (!QcPathTerrain::passable(m_window, landing, rules, false)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

int QcPathFinder::expand(QcBlockPos at, QcPathRules rules, Moves &moves)
{
    const QcPathStand here = QcPathTerrain::standAt(m_window, at, rules);
    int count = 0;
    float sideCost = 0.0f;
    std::array<bool, 4> flat{};
    for (std::size_t i = 0; i < sides.size(); ++i) {
        const QcBlockPos side = at.offset(sides[i][0], 0, sides[i][1]);
        const std::optional<Move> move = sideStep(at, here, side, rules);
        if (!move) {
            sideCost += dangerous(side) ? dangerBesideCost : wallCost;
            continue;
        }
        moves[std::size_t(count++)] = *move;
        flat[i] = move->to == side && sameLevelAs(QcPathTerrain::standAt(m_window, side, rules), here);
    }

    count = climb(at, rules, moves, count);

    // Diagonals only between two free sides on the same level: no cutting corners.
    for (std::size_t a = 0; a < 2; ++a) {
        for (std::size_t b = 2; b < 4; ++b) {
            if (!flat[a] || !flat[b]) {
                continue;
            }
            const QcBlockPos corner = at.offset(sides[a][0], 0, sides[b][1]);
            const QcPathStand stand = QcPathTerrain::standAt(m_window, corner, rules);
            if (stand.ok && sameLevelAs(stand, here)) {
                moves[std::size_t(count++)] = Move{corner, diagonalCost + standCost(stand, rules)};
            }
        }
    }

    for (int i = 0; i < count; ++i) {
        moves[std::size_t(i)].cost += sideCost;
    }
    return count;
}

int QcPathFinder::climb(QcBlockPos at, QcPathRules rules, Moves &moves, int count)
{
    if (QcPathTerrain::kindOf(m_window.cell(at), rules) != QcPathCell::Kind::Ladder) {
        return count;
    }

    // The top of a ladder is left sideways (sideStep, a step up of a block).
    const QcBlockPos up = at.offset(0, 1, 0);
    if (QcPathTerrain::kindOf(m_window.cell(up), rules) == QcPathCell::Kind::Ladder && QcPathTerrain::standAt(m_window, up, rules).ok) {
        moves[std::size_t(count++)] = Move{up, 1.0f + climbCost};
    }
    const QcBlockPos down = at.offset(0, -1, 0);
    const QcPathStand below = QcPathTerrain::standAt(m_window, down, rules);
    if (below.ok) {
        moves[std::size_t(count++)] = Move{down, 1.0f + standCost(below, rules)};
    }
    return count;
}

bool QcPathFinder::dangerous(QcBlockPos side)
{
    return QcPathCell::kindOf(m_window.cell(side)) == QcPathCell::Kind::Danger
           || QcPathCell::kindOf(m_window.cell(side.offset(0, -1, 0))) == QcPathCell::Kind::Danger;
}

QcPathResult QcPathFinder::find(const QcBlockAccess &world, const QcPathCellTable &table, QcPathRules rules,
                                const QcPathRequest &request)
{
    m_window.begin(world, table, request.start);
    const std::optional<QcBlockPos> start = startCell(request.start, rules);
    if (!start) {
        return {};
    }

    const int startIndex = m_window.indexOf(*start);
    m_window.open(startIndex, 0.0f, -1);
    m_heap.clear();
    m_heap.emplace_back(estimate(*start, request.goal) * heuristicWeight, startIndex);
    int best = startIndex;
    float bestEstimate = estimate(*start, request.goal);
    int nodes = 0;
    Moves moves;
    const auto later = std::greater<>();

    while (!m_heap.empty() && nodes < request.maxNodes) {
        std::ranges::pop_heap(m_heap, later);
        const int index = m_heap.back().second;
        m_heap.pop_back();
        if (m_window.closed(index)) {
            continue;
        }
        m_window.close(index);
        ++nodes;

        const QcBlockPos at = m_window.positionOf(index);
        const float remaining = estimate(at, request.goal);
        if (remaining < bestEstimate || (remaining == bestEstimate && m_window.cost(index) < m_window.cost(best))) {
            best = index;
            bestEstimate = remaining;
        }
        if (isThere(at, request)) {
            best = index;
            break;
        }

        const int count = expand(at, rules, moves);
        for (int i = 0; i < count; ++i) {
            const int next = m_window.indexOf(moves[std::size_t(i)].to);
            const float cost = m_window.cost(index) + moves[std::size_t(i)].cost;
            if (next < 0 || m_window.closed(next) || (m_window.visited(next) && m_window.cost(next) <= cost)) {
                continue;
            }
            m_window.open(next, cost, index);
            m_heap.emplace_back(cost + estimate(moves[std::size_t(i)].to, request.goal) * heuristicWeight, next);
            std::ranges::push_heap(m_heap, later);
        }
    }

    QcPathResult result = pathTo(best, startIndex, request);
    result.nodes = nodes;
    return result;
}

QcPathResult QcPathFinder::pathTo(int end, int startIndex, const QcPathRequest &request)
{
    QcPathResult result;
    result.started = true;
    for (int index = end; index != startIndex && index >= 0; index = m_window.parent(index)) {
        result.cells.push_back(m_window.positionOf(index));
    }
    std::ranges::reverse(result.cells);

    const bool truncated = std::cmp_greater(result.cells.size(), request.maxLength);
    if (truncated) {
        result.cells.resize(std::size_t(std::max(0, request.maxLength)));
    }
    result.reachesGoal = !truncated && isThere(m_window.positionOf(end), request);
    return result;
}
