// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/physics/QcPhysics.h"
#include "server/pathing/QcMobPath.h"
#include "server/pathing/QcPathGround.h"

#include <optional>

struct QcCreature;

// How creatures go through open doors and trapdoors (decision E-905), part of QcMobNavigator: a
// panel standing in a cell (an open door's leaf is 3/16 block thick) leaves a 0.6-wide body only
// 1.25 cm beside it at the cell's middle, so the creature steers to the middle of the free opening
// beside the panel (QcPathGap) instead, and keeps to it as long as its body overlaps the cell.
namespace QcOpeningSteering {

// A stretch along one horizontal axis in world coordinates.
struct Span
{
    double from = 0.0;
    double to = 0.0;

    double middle() const { return (from + to) / 2.0; }
};

// Where the creature may go across the way, per horizontal axis, in world coordinates: within the
// openings of the doors its body overlaps and of its next waypoint; none along a free axis.
struct Lane
{
    std::optional<Span> x;
    std::optional<Span> z;
};

// What the creature steers to for its next waypoint this tick: the waypoint's point (arrival and
// progress are measured to it) and the lane its body keeps to on the way.
struct Target
{
    QcVec3 point;
    Lane lane;
};

// The gap of the cells a body of the creature's height fills standing in `cell`.
QcPathGap gapAt(QcBlockPos cell, const QcPathGround &ground);
// The point of a waypoint: the middle of its cell (of the body's columns, QcPathTerrain::pointOf, and
// for a wide wall climber going up or down at the wall, QcPathTerrain::climbPoint), moved to the middle of the opening along the
// axes the gap it keeps to narrows (`gap`: the gap of its cell where the path goes through along the
// panel, else across the line of the path that of the waypoint just before or after it in line);
// for a waypoint right before or after a door (narrowed only by its neighbour in line), the axes
// it lines the creature up along.
struct Point
{
    QcVec3 at;
    QcPathGap gap;
    bool linesUpX = false;
    bool linesUpZ = false;

    // Whether a creature at `position` is lined up (within laneTolerance across the line through
    // the opening): a waypoint right before a door counts as reached only then, so the creature does
    // not go on into the doorway from off the line (coming in steeply from the side). The navigator
    // drops the condition for a creature that makes no progress (pushed, knocked back, too wide).
    bool onLane(const QcVec3 &position) const;
};
// The point of waypoint `index`.
Point waypointPoint(const QcMobPath &path, std::size_t index, const QcPathGround &ground);
// The target for the next waypoint, whose point is `point`: the lane is the point's gap within the
// gaps of the cells near it on the path that the creature's body overlaps, so it does not slide
// into a leaf it is still beside (a next door hinged on the other side, a turn).
Target target(const QcCreature &mob, const Point &point, const QcPathGround &ground);
// The steering towards a waypoint that lines the creature up: across the line, at most as fast as
// reaches the line in one tick (a fast creature would otherwise step across it and back forever).
QcPhysics::Steering lineUp(const QcCreature &mob, const Point &point, QcPhysics::Steering steering);
// Where the creature heads for the target: along a lane narrowed on one axis, onto the lane's
// middle at most lookAhead ahead along the other (one coming in off the line lines up before it
// reaches the panel); otherwise the point, moved to the middles of the lane.
QcVec3 aimAt(const QcCreature &mob, const Target &target);

// How far ahead along the lane a creature heads while it lines up with the opening: one off the
// lane halves its distance to it about every sixth of a block.
inline constexpr double lookAhead = 0.25;
// How far across the line through an opening a creature may be and have reached its waypoint.
inline constexpr double laneTolerance = 0.05;

} // namespace QcOpeningSteering
