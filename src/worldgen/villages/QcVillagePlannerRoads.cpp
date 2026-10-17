#include "worldgen/villages/QcVillagePlanner.h"

#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"

#include <algorithm>
#include <climits>
#include <functional>

// The road laying of the village planner (decision E-408): roads in eight directions, bending and
// winding with the terrain, recorded cell by cell, and pieces beside diagonal roads.

namespace {

// A winding road looks this far ahead for the flatter direction, and turns at most every few cells.
constexpr int windLook = 6;
constexpr int windGapMinimum = 6;
constexpr int windGapRange = 6;
// A piece beside a diagonal road stands at most this far from its middle line, and its forecourt
// reaches at most this far.
constexpr int maxSetback = 12;
constexpr int maxForecourt = 12;

// The columns (x when the front row runs along x, else z) of the doorsteps in the row `row` in
// front of a piece; `middle` when none lies there.
QList<int> doorColumns(const QList<QcBlockPos> &doorsteps, bool alongX, int row, int middle)
{
    QList<int> doors;
    for (const QcBlockPos &at : doorsteps) {
        if ((alongX ? at.z : at.x) == row) {
            doors.append(alongX ? at.x : at.z);
        }
    }
    if (doors.isEmpty()) {
        doors.append(middle);
    }
    return doors;
}

// The distance of two directions in eighths of a turn (0 to 4).
int turnDistance(int a, int b)
{
    const int d = std::abs(a - b) % 8;
    return std::min(d, 8 - d);
}

} // namespace

QList<QcVillagePlanner::Road> QcVillagePlanner::layArm(Layout &layout, const Arm &arm) const
{
    QList<Road> runs = walkArm(layout, arm);
    if (runs.isEmpty()) {
        return runs;
    }

    recordRuns(layout, runs, arm.closed);
    if (layout.style->overWater && !arm.closed) {
        layStairs(layout, runs.back());
    }
    return runs;
}

QList<QcVillagePlanner::Road> QcVillagePlanner::walkArm(Layout &layout, const Arm &arm) const
{
    QList<Road> runs;
    int direction = arm.direction;
    int heading = arm.direction;
    std::pair<int, int> cell{arm.x, arm.z};
    int previous = m_terrain.column(arm.x, arm.z).height;
    qsizetype bend = 0;
    int nextWind = arm.winding ? arm.firstSlot + windGapMinimum + int(random(layout) * windGapRange) : -1;
    Road run{arm.x, arm.z, directions[direction].first, directions[direction].second, 0, arm.firstSlot, arm.branch, 0, false, arm.sides};
    int laid = 0;
    for (int d = 0; d < arm.length; ++d) {
        int next = direction;
        if (bend < arm.bends.size() && arm.bends[bend].first == d) {
            next = (direction + arm.bends[bend++].second + 8) % 8;
            heading = next;
        } else if (d == nextWind) {
            next = (direction + windTurn(layout, cell, direction, heading) + 8) % 8;
            nextWind += windGapMinimum + int(random(layout) * windGapRange);
        }

        const auto [dx, dz] = directions[next];
        const std::pair<int, int> at = d == 0 ? cell : std::pair{cell.first + dx, cell.second + dz};
        const std::optional<int> height = roadGround(layout, at, next, previous, d >= arm.firstSlot);
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

std::optional<int> QcVillagePlanner::roadGround(const Layout &layout, std::pair<int, int> cell, int direction, int previous,
                                                bool checkPieces) const
{
    const auto [x, z] = cell;
    const QcTerrainColumn column = m_terrain.column(x, z);
    // Over water a road is a boardwalk at the floor height: it ends where the ground cannot carry it.
    const bool ends = layout.style->overWater ? !QcBoardwalkPlanner::carries(m_terrain, layout.style->stilts, x, z)
                                              : column.height - 1 < m_terrain.seaLevel() || std::abs(column.height - previous) > roadStep;
    const auto [dx, dz] = directions[direction];
    const Rect rect = brush(cell, dx, dz, dx != 0 && dz != 0);
    const bool blocked = checkPieces && std::ranges::any_of(layout.pieces, [&](Rect piece) { return piece.intersects(rect, 0); });
    if (ends || blocked || !insideRadius(layout, rect)) {
        return std::nullopt;
    }
    return column.height;
}

int QcVillagePlanner::windTurn(Layout &layout, std::pair<int, int> cell, int direction, int heading) const
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
        const bool wet = !layout.style->overWater && ahead.height - 1 < m_terrain.seaLevel();
        const int score = std::abs(ahead.height - here) + (wet ? 100 : 0);
        if (score < best) {
            best = score;
            count = 0;
        }
        if (score == best) {
            flattest[std::size_t(count++)] = turn;
        }
    }
    return flattest[std::size_t(std::min(count - 1, qsizetype(random(layout) * double(count))))];
}

void QcVillagePlanner::recordRuns(Layout &layout, const QList<Road> &runs, bool closed) const
{
    const int arm = layout.plan.runs.isEmpty() ? 0 : layout.plan.runs.back().arm + 1;
    for (qsizetype i = 0; i < runs.size(); ++i) {
        // Where runs meet (and all round a ring) the brush is square, so a bend has no gap.
        const Road &run = runs[i];
        paintRun(layout, run, {i > 0 || closed, i + 1 < runs.size() || closed});

        const int endX = run.x + run.dx * (run.length - 1);
        const int endZ = run.z + run.dz * (run.length - 1);
        const Rect first = brush({run.x, run.z}, run.dx, run.dz, true);
        const Rect last = brush({endX, endZ}, run.dx, run.dz, true);
        layout.roads.append({std::min(first.x0, last.x0), std::min(first.z0, last.z0), std::max(first.x1, last.x1), std::max(first.z1, last.z1)});
        layout.plan.runs.append({run.x, run.z, run.dx, run.dz, run.length, arm, run.branch});
        layout.plan.roadLength += run.length;
    }
}

void QcVillagePlanner::paintRun(Layout &layout, const Road &run, std::pair<bool, bool> joins) const
{
    for (int d = 0; d < run.length; ++d) {
        const bool joint = (d == 0 && joins.first) || (d == run.length - 1 && joins.second);
        const std::pair<int, int> middle{run.x + run.dx * d, run.z + run.dz * d};
        const Rect rect = brush(middle, run.dx, run.dz, run.diagonal() || joint);
        // The cells across the road lie at the height of its middle line where the ground allows.
        const int level = m_terrain.column(middle.first, middle.second).height - 1;
        for (int z = rect.z0; z <= rect.z1; ++z) {
            for (int x = rect.x0; x <= rect.x1; ++x) {
                addRoadCell(layout, {x, z}, run.branch, level);
            }
        }
    }
}

void QcVillagePlanner::addRoadCell(Layout &layout, std::pair<int, int> cell, bool branch, std::optional<int> level) const
{
    const auto [x, z] = cell;
    const bool underPiece = std::ranges::any_of(layout.pieces, [&](Rect piece) { return piece.intersects({x, z, x, z}, 0); });
    if (underPiece || layout.roadCells.contains(cellKey(x, z))) {
        return;
    }

    layout.roadCells.insert(cellKey(x, z));
    if (!layout.style->overWater) {
        const int ground = m_terrain.column(x, z).height - 1;
        const int y = level && std::abs(*level - ground) <= 1 ? *level : ground;
        (branch ? layout.plan.branchRoads : layout.plan.roads).append({x, y, z});
    } else {
        const QcBlockId block = branch ? layout.plan.branchPathBlock : layout.plan.pathBlock;
        layout.plan.decks.append(QcBoardwalkPlanner::deck(m_terrain, layout.style->stilts, {x, z}, block));
    }
}

QcVillagePlanner::Rect QcVillagePlanner::brush(std::pair<int, int> cell, int dx, int dz, bool square)
{
    const auto [x, z] = cell;
    if (square) {
        return {x - roadHalfWidth, z - roadHalfWidth, x + roadHalfWidth, z + roadHalfWidth};
    }
    // Across an axis run only.
    return {x - roadHalfWidth * std::abs(dz), z - roadHalfWidth * std::abs(dx), x + roadHalfWidth * std::abs(dz), z + roadHalfWidth * std::abs(dx)};
}

void QcVillagePlanner::markRoad(Layout &layout, Rect rect) const
{
    for (int z = rect.z0; z <= rect.z1; ++z) {
        for (int x = rect.x0; x <= rect.x1; ++x) {
            layout.roadCells.insert(cellKey(x, z));
        }
    }
    layout.roads.append(rect);
}

bool QcVillagePlanner::touchesRoad(const Layout &layout, Rect rect, int gap) const
{
    for (int z = rect.z0 - gap; z <= rect.z1 + gap; ++z) {
        for (int x = rect.x0 - gap; x <= rect.x1 + gap; ++x) {
            if (layout.roadCells.contains(cellKey(x, z))) {
                return true;
            }
        }
    }
    return false;
}

int QcVillagePlanner::clearOfCentre(const Layout &layout, int direction) const
{
    const Rect centre = layout.pieces.front();
    const auto [dx, dz] = directions[direction];
    int d = 1;
    while (d < m_settings.radius && centre.intersects(brush({layout.centreX + dx * d, layout.centreZ + dz * d}, dx, dz, dx != 0 && dz != 0), 0)) {
        ++d;
    }
    return d;
}

int QcVillagePlanner::placeAtDiagonal(Layout &layout, const Road &road, std::pair<int, int> alongAndSide, int index) const
{
    const auto [along, side] = alongAndSide;
    const int cx = road.x + road.dx * along;
    const int cz = road.z + road.dz * along;
    // Away from the road on this side is diagonal; the door faces back along one of its axes.
    const int awayX = -road.dz * side;
    const int awayZ = road.dx * side;
    const std::pair<int, int> facing = random(layout) < 0.5 ? std::pair{-awayX, 0} : std::pair{0, -awayZ};
    const QcVillageStyle::Blueprint &blueprint = chooseBlueprint(layout, layout.style->pieces[index]);
    const int turns = turnsFacing(facing.first, facing.second);
    const int sizeX = turns % 2 == 0 ? blueprint.structure()->sizeX() : blueprint.structure()->sizeZ();
    const int sizeZ = turns % 2 == 0 ? blueprint.structure()->sizeZ() : blueprint.structure()->sizeX();

    for (int setback = roadHalfWidth + 2; setback <= maxSetback; ++setback) {
        // The middle of the front row, straight back from the road's middle line.
        const int mx = cx - facing.first * setback;
        const int mz = cz - facing.second * setback;
        const Rect rect = behindFront({mx, mz}, facing, {sizeX, sizeZ});
        if (touchesRoad(layout, rect, 1)) {
            continue;
        }

        const bool hitsPiece = std::ranges::any_of(layout.pieces, [&](Rect other) { return rect.intersects(other, pieceGap); });
        const std::optional<int> floor = hitsPiece || !insideRadius(layout, rect) ? std::nullopt : floorFor(layout, rect, m_settings.maxPieceSlope);
        // The doors of the blueprint as written (its variations keep them in place).
        const QList<QcBlockPos> steps = floor ? doorsteps(QcStructurePlacement(blueprint.structure(), {rect.x0, *floor, rect.z0}, turns, m_content))
                                              : QList<QcBlockPos>{};
        const QList<std::pair<int, int>> court = floor ? forecourt(layout, rect, facing, steps) : QList<std::pair<int, int>>{};
        if (!floor || court.isEmpty()) {
            return 0;
        }
        addPiece(layout, blueprint, rect, {rect.x0, *floor, rect.z0}, turns);
        for (const std::pair<int, int> &cell : court) {
            addRoadCell(layout, cell, true);
        }
        return std::max(sizeX, sizeZ);
    }
    return 0;
}

QcVillagePlanner::Rect QcVillagePlanner::behindFront(std::pair<int, int> middle, std::pair<int, int> facing, std::pair<int, int> size)
{
    const auto [mx, mz] = middle;
    const auto [sizeX, sizeZ] = size;
    const int x0 = facing.first == 0 ? mx - sizeX / 2 : (facing.first < 0 ? mx : mx - sizeX + 1);
    const int z0 = facing.second == 0 ? mz - sizeZ / 2 : (facing.second < 0 ? mz : mz - sizeZ + 1);
    return {x0, z0, x0 + sizeX - 1, z0 + sizeZ - 1};
}

QList<std::pair<int, int>> QcVillagePlanner::forecourt(const Layout &layout, Rect rect, std::pair<int, int> facing,
                                                       const QList<QcBlockPos> &doorsteps) const
{
    // Straight out to the road from the columns of the front row next to a door: those beside it
    // that do not reach the road within the forecourt's reach stay grass, the door's own must.
    const bool alongX = facing.first == 0;
    const int low = alongX ? rect.x0 : rect.z0;
    const int high = alongX ? rect.x1 : rect.z1;
    const int front = alongX ? (facing.second < 0 ? rect.z0 : rect.z1) : (facing.first < 0 ? rect.x0 : rect.x1);
    const int step = alongX ? facing.second : facing.first;
    const QList<int> doors = doorColumns(doorsteps, alongX, front + step, (low + high) / 2);

    QList<std::pair<int, int>> cells;
    for (int u = low; u <= high; ++u) {
        if (std::ranges::none_of(doors, [u](int door) { return std::abs(u - door) <= roadHalfWidth; })) {
            continue;
        }
        const auto cellAt = [&](int j) { return alongX ? std::pair{u, front + step * j} : std::pair{front + step * j, u}; };
        const std::optional<QList<std::pair<int, int>>> column = forecourtColumn(layout, cellAt);
        if (!column || (column->isEmpty() && doors.contains(u))) {
            return {};
        }
        cells.append(*column);
    }
    return cells;
}

std::optional<QList<std::pair<int, int>>> QcVillagePlanner::forecourtColumn(const Layout &layout,
                                                                           const std::function<std::pair<int, int>(int)> &cellAt) const
{
    QList<std::pair<int, int>> column;
    for (int j = 1; j <= maxForecourt; ++j) {
        const auto [x, z] = cellAt(j);
        if (layout.roadCells.contains(cellKey(x, z))) {
            return column;
        }
        const bool onPiece = std::ranges::any_of(layout.pieces, [&](Rect piece) { return piece.intersects({x, z, x, z}, 0); });
        if (onPiece || m_terrain.column(x, z).height - 1 < m_terrain.seaLevel()) {
            return std::nullopt;
        }
        column.append({x, z});
    }
    return QList<std::pair<int, int>>{};
}
