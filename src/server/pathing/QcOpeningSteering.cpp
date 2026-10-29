// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/pathing/QcOpeningSteering.h"

#include "server/entities/QcCreature.h"
#include "server/pathing/QcPathSteps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace QcOpeningSteering {

// A body planned through a door (QcPathRules::fitsThrough) keeps clear of the panel while lined up.
static_assert(QcPathRules::panelClearance >= laneTolerance);

namespace {

// An opening of a cell at `corner` (its x or z) in world coordinates.
std::optional<Span> spanOf(std::optional<QcPathOpening> opening, int corner)
{
    if (!opening) {
        return std::nullopt;
    }
    return Span{corner + static_cast<double>(opening->from), corner + static_cast<double>(opening->to)};
}

// Both spans at once: where they do not overlap, the empty stretch between their inner edges (its
// middle lies as far from both panels as can be).
std::optional<Span> within(std::optional<Span> a, std::optional<Span> b)
{
    if (a && b) {
        return Span{std::max(a->from, b->from), std::min(a->to, b->to)};
    }
    return a ? a : b;
}

Lane laneOf(QcBlockPos cell, const QcPathGap &gap)
{
    return {spanOf(gap.x, cell.x), spanOf(gap.z, cell.z)};
}

Lane within(const Lane &a, const Lane &b)
{
    return {within(a.x, b.x), within(a.z, b.z)};
}

// Whether the creature's body overlaps the column of `cell` (seen from above).
bool overlaps(const QcCreature &mob, QcBlockPos cell, double halfWidth)
{
    const QcVec3 &at = mob.motion.position;
    return at.x + halfWidth > cell.x && at.x - halfWidth < cell.x + 1 && at.z + halfWidth > cell.z && at.z - halfWidth < cell.z + 1;
}

// Whether the step between two path cells runs along z (`alongZ`) or x: a panel standing along it
// narrows the way, one standing across it is passed through (vertical steps run along both).
bool stepRuns(QcBlockPos from, QcBlockPos to, bool alongZ)
{
    return alongZ ? from.x == to.x : from.z == to.z;
}

// The gap of waypoint `index` as the path goes through it: an axis narrowed by a panel counts only
// where the steps into and out of the cell run along the panel (a door walked through), not across
// it (a leaf or a shutter beside a turn).
QcPathGap throughGap(const QcMobPath &path, std::size_t index, const QcPathGround &ground)
{
    QcPathGap gap = gapAt(path.cells[index], ground);
    if (!gap.narrows()) {
        return gap;
    }
    for (const std::size_t other : {index - 1, index + 1}) {
        // index - 1 wraps around to a huge value for the first waypoint.
        if (other >= path.cells.size()) {
            continue;
        }
        gap.x = stepRuns(path.cells[index], path.cells[other], true) ? gap.x : std::nullopt;
        gap.z = stepRuns(path.cells[index], path.cells[other], false) ? gap.z : std::nullopt;
    }
    return gap;
}

// The gap kept at waypoint `index` whose cell has the gap `own`: across the line of the path, that
// of the waypoint just before or after it in line where its own cell has none.
QcPathGap keptGap(const QcMobPath &path, std::size_t index, QcPathGap gap, const QcPathGround &ground)
{
    const QcBlockPos cell = path.cells[index];
    for (const std::size_t other : {index - 1, index + 1}) {
        if (other >= path.cells.size() || (gap.x && gap.z)) {
            continue;
        }
        const QcBlockPos neighbour = path.cells[other];
        const int dx = neighbour.x - cell.x;
        const int dz = neighbour.z - cell.z;
        const bool lineZ = dx == 0 && std::abs(dz) == 1;
        const bool lineX = dz == 0 && std::abs(dx) == 1;
        if (!lineZ && !lineX) {
            continue;
        }
        // In line along z, the cells share their x: the neighbour's opening across x is this one's.
        const QcPathGap beside = throughGap(path, other, ground);
        gap.x = lineZ && !gap.x ? beside.x : gap.x;
        gap.z = lineX && !gap.z ? beside.z : gap.z;
    }
    return gap;
}

// Whether the path goes straight up or down at waypoint `index` (from the one before it or to the one after it).
bool vertical(const QcMobPath &path, std::size_t index)
{
    const QcBlockPos cell = path.cells[index];
    return std::ranges::any_of(std::array{index - 1, index + 1}, [&](std::size_t other) {
        return other < path.cells.size() && path.cells[other].x == cell.x && path.cells[other].z == cell.z;
    });
}

// The middle of the body's columns at waypoint `index` (decision E-909): the middle of its cell, of
// four cells for a body wider than a block, and on the way up or down a wall or along it (held only
// by clinging) towards the wall.
QcVec3 middleOf(const QcMobPath &path, std::size_t index, const QcPathGround &ground)
{
    const QcBlockPos cell = path.cells[index];
    if (ground.rules.climbsWalls && ground.rules.footprint() > 1) {
        const QcPathDirectCells cells(ground.world, ground.cells);
        if (vertical(path, index) || QcPathTerrain::clingsOnly(cells, cell, ground.rules)) {
            return QcPathTerrain::climbPoint(cells, cell, ground.rules);
        }
    }
    return QcPathTerrain::pointOf(cell, ground.rules);
}

// Waypoints before the next one whose doors the body may still overlap, and after it.
constexpr std::size_t behind = 2;
constexpr std::size_t ahead = 1;

} // namespace

QcPathGap gapAt(QcBlockPos cell, const QcPathGround &ground)
{
    QcPathGap gap;
    const int cells = std::max(1, static_cast<int>(std::ceil(ground.rules.height - 1e-6)));
    for (int dy = 0; dy < cells; ++dy) {
        const std::optional<QcBlockId> block = ground.world.blockAt(cell.offset(0, dy, 0));
        if (block) {
            gap = gap.within(ground.cells.gap(*block));
        }
    }
    return gap;
}

bool Point::onLane(const QcVec3 &position) const
{
    return (!linesUpX || std::abs(position.x - at.x) <= laneTolerance) && (!linesUpZ || std::abs(position.z - at.z) <= laneTolerance);
}

Point waypointPoint(const QcMobPath &path, std::size_t index, const QcPathGround &ground)
{
    const QcBlockPos cell = path.cells[index];
    const QcPathGap own = throughGap(path, index, ground);
    const QcPathGap gap = keptGap(path, index, own, ground);
    const QcVec3 middle = middleOf(path, index, ground);
    const QcVec3 at{gap.x ? cell.x + static_cast<double>(gap.x->middle()) : middle.x, static_cast<double>(cell.y),
                    gap.z ? cell.z + static_cast<double>(gap.z->middle()) : middle.z};
    return {at, gap, gap.x && !own.x, gap.z && !own.z};
}

Target target(const QcCreature &mob, const Point &point, const QcPathGround &ground)
{
    const QcMobPath &path = mob.path;
    const std::size_t next = path.next;
    Lane lane = laneOf(path.cells[next], point.gap);

    // The doors the body is still in or already in (two doors in a row, a turn right behind one).
    const double halfWidth = ground.rules.width / 2.0;
    const std::size_t first = next >= behind ? next - behind : 0;
    const std::size_t last = std::min(path.cells.size(), next + ahead + 1);
    for (std::size_t index = first; index < last; ++index) {
        if (index != next && overlaps(mob, path.cells[index], halfWidth)) {
            lane = within(laneOf(path.cells[index], throughGap(path, index, ground)), lane);
        }
    }
    return {point.at, lane};
}

QcPhysics::Steering lineUp(const QcCreature &mob, const Point &point, QcPhysics::Steering steering)
{
    if (steering.speed <= 0.0) {
        return steering;
    }
    // At most the distance to the line in one tick (the velocity on the ground follows the wish
    // within a tick): a fast creature does not step across the line and back again.
    const auto cap = [&](double wish, double offset) {
        const double most = std::abs(offset) * QcPhysics::tickRate / steering.speed;
        return std::clamp(wish, -most, most);
    };
    const QcVec3 &at = mob.motion.position;
    if (point.linesUpX) {
        steering.wish.x = cap(steering.wish.x, point.at.x - at.x);
    }
    if (point.linesUpZ) {
        steering.wish.z = cap(steering.wish.z, point.at.z - at.z);
    }
    return steering;
}

QcVec3 aimAt(const QcCreature &mob, const Target &target)
{
    const QcVec3 &at = mob.motion.position;
    const Lane &lane = target.lane;
    if (lane.x && !lane.z) {
        return {lane.x->middle(), target.point.y, at.z + std::clamp(target.point.z - at.z, -lookAhead, lookAhead)};
    }
    if (lane.z && !lane.x) {
        return {at.x + std::clamp(target.point.x - at.x, -lookAhead, lookAhead), target.point.y, lane.z->middle()};
    }
    return {lane.x ? lane.x->middle() : target.point.x, target.point.y, lane.z ? lane.z->middle() : target.point.z};
}

} // namespace QcOpeningSteering
