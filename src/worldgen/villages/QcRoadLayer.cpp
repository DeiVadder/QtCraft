// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/villages/QcRoadLayer.h"

#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"

#include <algorithm>
#include <climits>

namespace {

using Rect = QcVillageDraft::Rect;
constexpr auto &directions = QcVillageDraft::directions;

// A winding road looks this far ahead for the flatter direction, and turns at most every few cells.
constexpr int windLook = 6;
constexpr int windGapMinimum = 6;
constexpr int windGapRange = 6;

// The distance of two directions in eighths of a turn (0 to 4).
int turnDistance(int a, int b)
{
    const int d = std::abs(a - b) % 8;
    return std::min(d, 8 - d);
}

// The turns of an arm as it is walked: its bends and its winding turns. A bridge runs straight from
// bank to bank along the line QcRoadLayer::bridgeHeight() checked (decision E-841): a bend or a
// winding turn due on it (or on the step off it) waits until the road has left it straight on, and
// later bends move with it. A closed arm (a plaza's ring) cannot wait without missing its start: it
// ends there and is left out.
class ArmTurns
{
public:
    enum class Due
    {
        None,
        Bend,
        Wind,
        End,
    };

    ArmTurns(const QcRoadLayer::Arm &arm, int nextWind)
        : m_arm(arm)
        , m_nextWind(nextWind)
    {
    }

    // What is due at the arm's cell `d`, coming from a bridge or not.
    Due due(int d, bool onBridge)
    {
        const bool bendDue = m_bend < m_arm.bends.size() && m_arm.bends[m_bend].first + m_bendDelay == d;
        if (onBridge && bendDue && m_arm.closed) {
            return Due::End;
        }
        if (onBridge) {
            m_bendDelay += bendDue ? 1 : 0;
            m_nextWind += d == m_nextWind ? 1 : 0;
            return Due::None;
        }
        if (bendDue) {
            // A winding turn due on the same cell comes a cell later.
            m_nextWind += d == m_nextWind ? 1 : 0;
            return Due::Bend;
        }
        return d == m_nextWind ? Due::Wind : Due::None;
    }

    // The turn of the bend that is due (eighths of a turn); the next bend becomes due.
    int takeBend() { return m_arm.bends[m_bend++].second; }

    // A winding turn was taken: the next comes `gap` cells on.
    void windAfter(int gap) { m_nextWind += gap; }

private:
    const QcRoadLayer::Arm &m_arm;
    qsizetype m_bend = 0;
    int m_bendDelay = 0;
    int m_nextWind;
};

} // namespace

QcRoadLayer::QcRoadLayer(const QcTerrainGenerator &terrain, const QcContentRegistry &content)
    : m_terrain(terrain)
    , m_content(content)
{
}

QList<QcRoadLayer::Road> QcRoadLayer::layArm(QcVillageDraft &draft, const Arm &arm) const
{
    QList<Road> runs = walkArm(draft, arm);
    if (runs.isEmpty()) {
        return runs;
    }

    recordRuns(draft, runs, arm.closed);
    if (draft.style->overWater && !arm.closed) {
        if (const std::optional<QcVillageDraft::Stairs> stairs = QcBoardwalkPlanner::stairsAt(m_terrain, m_content, draft, runs.back())) {
            draft.addStairs(*stairs);
        }
    }
    return runs;
}

QList<QcRoadLayer::Road> QcRoadLayer::walkArm(QcVillageDraft &draft, const Arm &arm) const
{
    QList<Road> runs;
    int direction = arm.direction;
    int heading = arm.direction;
    std::pair<int, int> cell{arm.x, arm.z};
    From from{draft.groundTop(m_terrain, arm.x, arm.z) + 1, 0, arm.direction};
    ArmTurns turns(arm, arm.winding ? arm.firstSlot + windGapMinimum + static_cast<int>(draft.random() * windGapRange) : -1);
    Road run{arm.x, arm.z, directions[direction].first, directions[direction].second, 0, arm.firstSlot, arm.branch, 0, false, arm.sides};
    int laid = 0;
    // A road on a bridge at the end of its length goes on to the far bank (decision E-841).
    for (int d = 0; d < arm.length || (from.bridgeCells > 0 && d < arm.length + maxBridge); ++d) {
        int next = direction;
        const ArmTurns::Due due = turns.due(d, from.bridgeCells > 0);
        if (due == ArmTurns::Due::End) {
            break;
        }
        if (due == ArmTurns::Due::Bend) {
            next = (direction + turns.takeBend() + 8) % 8;
            heading = next;
        } else if (due == ArmTurns::Due::Wind) {
            next = (direction + windTurn(draft, cell, direction, heading) + 8) % 8;
            turns.windAfter(windGapMinimum + static_cast<int>(draft.random() * windGapRange));
        }

        const auto [dx, dz] = directions[next];
        const std::pair<int, int> at = d == 0 ? cell : std::pair{cell.first + dx, cell.second + dz};
        const std::optional<Ground> ground = roadGround(draft, at, next, from, d >= arm.firstSlot);
        if (!ground) {
            break;
        }
        if (next != direction) {
            runs.append(run);
            run = Road{at.first, at.second, dx, dz, 0, 0, arm.branch, laid, false, arm.sides};
            direction = next;
        }
        ++run.length;
        ++laid;
        cell = at;
        from = {ground->height, ground->bridge ? from.bridgeCells + 1 : 0, next};
    }
    runs.append(run);
    // A bridge the road does not finish (its length, a piece or the radius ends it over the water)
    // is left out: the road ends at the bank.
    laid -= from.bridgeCells;
    runs = withoutLast(std::move(runs), from.bridgeCells);
    if (laid <= arm.firstSlot || (arm.closed && laid < arm.length)) {
        return {};
    }

    runs.back().end = !arm.closed;
    return runs;
}

std::optional<QcRoadLayer::Ground> QcRoadLayer::roadGround(const QcVillageDraft &draft, std::pair<int, int> cell, int direction,
                                                           From from, bool checkPieces) const
{
    const auto [x, z] = cell;
    const QcTerrainColumn column = m_terrain.column(x, z);
    // Over water a road is a boardwalk at the floor height: it ends where the ground cannot carry it.
    // On land it crosses rivers on bridges and ends at other water; the whole stretch of water is
    // checked at its first cell, the cells after it in the same direction lie on the same bridge.
    // Around a centre with blended ground (decision E-893) the road runs on the ground as built.
    Ground ground{draft.groundTop(m_terrain, x, z) + 1, false};
    if (!draft.style->overWater && column.height - 1 < m_terrain.seaLevel()) {
        const bool sameBridge = from.bridgeCells > 0 && from.direction == direction;
        const std::optional<int> deck = sameBridge ? std::optional<int>(bridgeDeck()) : bridgeHeight(draft, cell, direction);
        if (!deck) {
            return std::nullopt;
        }
        ground = {*deck, true};
    }
    // Onto and off a bridge the road steps at most a block, which villagers walk (decision E-411).
    const int step = ground.bridge || from.bridgeCells > 0 ? 1 : roadStep;
    const bool ends = draft.style->overWater ? !QcBoardwalkPlanner::carries(m_terrain, draft.style->stilts, column)
                                             : std::abs(ground.height - from.height) > step;
    const auto [dx, dz] = directions[direction];
    const Rect rect = QcVillageDraft::brush(cell, dx, dz, dx != 0 && dz != 0);
    const bool blocked = checkPieces && std::ranges::any_of(draft.pieces, [&](Rect piece) { return piece.intersects(rect, 0); });
    if (ends || blocked || !draft.insideRadius(rect)) {
        return std::nullopt;
    }
    // Roads and bridges keep off river deltas (decision E-850): no column of the cell's widest
    // brush (the square one of a bend) in a delta. Last, after the cheaper tests.
    const Rect square = QcVillageDraft::brush(cell, dx, dz, true);
    if (m_terrain.touchesDelta({square.x0, square.z0, square.x1, square.z1})) {
        return std::nullopt;
    }
    return ground;
}

QList<QcRoadLayer::Road> QcRoadLayer::withoutLast(QList<Road> runs, int cells)
{
    while (cells > 0 && !runs.isEmpty()) {
        const int cut = std::min(cells, runs.back().length);
        runs.back().length -= cut;
        cells -= cut;
        if (runs.back().length == 0 && runs.size() > 1) {
            runs.removeLast();
        } else if (cut == 0) {
            break;
        }
    }
    return runs;
}

QcRoadLayer::Stretch QcRoadLayer::wetStretch(std::pair<int, int> cell, int dx, int dz) const
{
    Stretch stretch;
    for (int i = 0; stretch.cells <= maxBridge; ++i) {
        const QcTerrainColumn column = m_terrain.column(cell.first + dx * i, cell.second + dz * i);
        if (column.height - 1 >= m_terrain.seaLevel()) {
            stretch.bankHeight = column.height;
            return stretch;
        }
        stretch.river = stretch.river || m_terrain.isRiverBiome(column.biome);
        stretch.delta = stretch.delta || m_terrain.isDeltaBiome(column.biome);
        ++stretch.cells;
    }
    return stretch;
}

std::optional<int> QcRoadLayer::bridgeHeight(const QcVillageDraft &draft, std::pair<int, int> cell, int direction) const
{
    if (draft.style->bridge.deck == qcAirBlockId) {
        return std::nullopt;
    }

    // The whole stretch of low bank and water along the road through this cell, back and on to the
    // first dry cells: some of its water a river's (a lake or the sea ends the road as before), at
    // most maxBridge cells, and the far bank about at the deck's height.
    const int deck = bridgeDeck();
    const auto [dx, dz] = directions[direction];
    const Stretch ahead = wetStretch(cell, dx, dz);
    const Stretch behind = wetStretch({cell.first - dx, cell.second - dz}, -dx, -dz);
    const bool fits = ahead.bankHeight && std::abs(*ahead.bankHeight - deck) <= 1 && ahead.cells + behind.cells <= maxBridge;
    // No bridge into the arms of a delta (decision E-850).
    const bool delta = ahead.delta || behind.delta;
    return fits && !delta && (ahead.river || behind.river) ? std::optional<int>(deck) : std::nullopt;
}

int QcRoadLayer::bridgeDeck() const
{
    return m_terrain.seaLevel() + 1;
}

QcVillageDraft::RoadLevel QcRoadLayer::roadLevel(const QcVillageDraft &draft, int x, int z) const
{
    // A road's middle line only runs over water and low bank on a bridge (roadGround).
    const QcTerrainColumn column = m_terrain.column(x, z);
    const bool bridge = column.height - 1 < m_terrain.seaLevel();
    return bridge ? QcVillageDraft::RoadLevel{m_terrain.seaLevel(), true} : QcVillageDraft::RoadLevel{draft.groundTop(m_terrain, x, z), false};
}

int QcRoadLayer::windTurn(QcVillageDraft &draft, std::pair<int, int> cell, int direction, int heading) const
{
    // The flattest of straight on and the two neighbouring directions within an eighth of the
    // heading; among equally flat ones a random one, so a road on level ground meanders too.
    const int here = m_terrain.column(cell.first, cell.second).height;
    std::array<int, 3> flattest{};
    qsizetype count = 0;
    int best = INT_MAX;
    for (const int turn : {0, -1, 1}) {
        const int candidate = (direction + turn + 8) % 8;
        if (turnDistance(candidate, heading) > 1) {
            continue;
        }
        const auto [dx, dz] = directions[candidate];
        const QcTerrainColumn ahead = m_terrain.column(cell.first + dx * windLook, cell.second + dz * windLook);
        const bool wet = !draft.style->overWater && ahead.height - 1 < m_terrain.seaLevel();
        const int score = std::abs(ahead.height - here) + (wet ? 100 : 0);
        if (score < best) {
            best = score;
            count = 0;
        }
        if (score == best) {
            flattest[static_cast<std::size_t>(count++)] = turn;
        }
    }
    return flattest[static_cast<std::size_t>(std::min(count - 1, static_cast<qsizetype>(draft.random() * static_cast<double>(count))))];
}

void QcRoadLayer::recordRuns(QcVillageDraft &draft, const QList<Road> &runs, bool closed) const
{
    const int arm = draft.nextArm();
    for (qsizetype i = 0; i < runs.size(); ++i) {
        // Where runs meet (and all round a ring) the brush is square, so a bend has no gap.
        const Road &run = runs[i];
        paintRun(draft, run, {i > 0 || closed, i + 1 < runs.size() || closed});
        draft.addRun(run, arm);
    }
}

void QcRoadLayer::paintRun(QcVillageDraft &draft, const Road &run, std::pair<bool, bool> joins) const
{
    for (int d = 0; d < run.length; ++d) {
        const bool joint = (d == 0 && joins.first) || (d == run.length - 1 && joins.second);
        const std::pair<int, int> middle{run.x + run.dx * d, run.z + run.dz * d};
        const Rect rect = QcVillageDraft::brush(middle, run.dx, run.dz, run.diagonal() || joint);
        // The cells across the road lie at the height of its middle line where the ground allows.
        const QcVillageDraft::RoadLevel level = roadLevel(draft, middle.first, middle.second);
        for (int z = rect.z0; z <= rect.z1; ++z) {
            for (int x = rect.x0; x <= rect.x1; ++x) {
                draft.addRoadCell(m_terrain, {x, z}, run.branch, level);
            }
        }
    }
}
