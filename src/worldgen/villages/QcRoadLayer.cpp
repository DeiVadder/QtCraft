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
        QcBoardwalkPlanner::layStairs(m_terrain, m_content, draft, runs.back());
    }
    return runs;
}

QList<QcRoadLayer::Road> QcRoadLayer::walkArm(QcVillageDraft &draft, const Arm &arm) const
{
    QList<Road> runs;
    int direction = arm.direction;
    int heading = arm.direction;
    std::pair<int, int> cell{arm.x, arm.z};
    int previous = m_terrain.column(arm.x, arm.z).height;
    qsizetype bend = 0;
    int nextWind = arm.winding ? arm.firstSlot + windGapMinimum + int(draft.random() * windGapRange) : -1;
    Road run{arm.x, arm.z, directions[direction].first, directions[direction].second, 0, arm.firstSlot, arm.branch, 0, false, arm.sides};
    int laid = 0;
    for (int d = 0; d < arm.length; ++d) {
        int next = direction;
        if (bend < arm.bends.size() && arm.bends[bend].first == d) {
            next = (direction + arm.bends[bend++].second + 8) % 8;
            heading = next;
        } else if (d == nextWind) {
            next = (direction + windTurn(draft, cell, direction, heading) + 8) % 8;
            nextWind += windGapMinimum + int(draft.random() * windGapRange);
        }

        const auto [dx, dz] = directions[next];
        const std::pair<int, int> at = d == 0 ? cell : std::pair{cell.first + dx, cell.second + dz};
        const std::optional<int> height = roadGround(draft, at, next, previous, d >= arm.firstSlot);
        if (!height) {
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
        previous = *height;
    }
    runs.append(run);
    if (laid <= arm.firstSlot || (arm.closed && laid < arm.length)) {
        return {};
    }

    runs.back().end = !arm.closed;
    return runs;
}

std::optional<int> QcRoadLayer::roadGround(const QcVillageDraft &draft, std::pair<int, int> cell, int direction, int previous,
                                           bool checkPieces) const
{
    const auto [x, z] = cell;
    const QcTerrainColumn column = m_terrain.column(x, z);
    // Over water a road is a boardwalk at the floor height: it ends where the ground cannot carry it.
    const bool ends = draft.style->overWater ? !QcBoardwalkPlanner::carries(m_terrain, draft.style->stilts, x, z)
                                             : column.height - 1 < m_terrain.seaLevel() || std::abs(column.height - previous) > roadStep;
    const auto [dx, dz] = directions[direction];
    const Rect rect = QcVillageDraft::brush(cell, dx, dz, dx != 0 && dz != 0);
    const bool blocked = checkPieces && std::ranges::any_of(draft.pieces, [&](Rect piece) { return piece.intersects(rect, 0); });
    if (ends || blocked || !draft.insideRadius(rect)) {
        return std::nullopt;
    }
    return column.height;
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
            flattest[std::size_t(count++)] = turn;
        }
    }
    return flattest[std::size_t(std::min(count - 1, qsizetype(draft.random() * double(count))))];
}

void QcRoadLayer::recordRuns(QcVillageDraft &draft, const QList<Road> &runs, bool closed) const
{
    const int arm = draft.plan.runs.isEmpty() ? 0 : draft.plan.runs.back().arm + 1;
    for (qsizetype i = 0; i < runs.size(); ++i) {
        // Where runs meet (and all round a ring) the brush is square, so a bend has no gap.
        const Road &run = runs[i];
        paintRun(draft, run, {i > 0 || closed, i + 1 < runs.size() || closed});

        const int endX = run.x + run.dx * (run.length - 1);
        const int endZ = run.z + run.dz * (run.length - 1);
        const Rect first = QcVillageDraft::brush({run.x, run.z}, run.dx, run.dz, true);
        const Rect last = QcVillageDraft::brush({endX, endZ}, run.dx, run.dz, true);
        draft.roads.append({std::min(first.x0, last.x0), std::min(first.z0, last.z0), std::max(first.x1, last.x1), std::max(first.z1, last.z1)});
        draft.plan.runs.append({run.x, run.z, run.dx, run.dz, run.length, arm, run.branch});
        draft.plan.roadLength += run.length;
    }
}

void QcRoadLayer::paintRun(QcVillageDraft &draft, const Road &run, std::pair<bool, bool> joins) const
{
    for (int d = 0; d < run.length; ++d) {
        const bool joint = (d == 0 && joins.first) || (d == run.length - 1 && joins.second);
        const std::pair<int, int> middle{run.x + run.dx * d, run.z + run.dz * d};
        const Rect rect = QcVillageDraft::brush(middle, run.dx, run.dz, run.diagonal() || joint);
        // The cells across the road lie at the height of its middle line where the ground allows.
        const int level = m_terrain.column(middle.first, middle.second).height - 1;
        for (int z = rect.z0; z <= rect.z1; ++z) {
            for (int x = rect.x0; x <= rect.x1; ++x) {
                draft.addRoadCell(m_terrain, {x, z}, run.branch, level);
            }
        }
    }
}
