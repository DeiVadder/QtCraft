#include "server/pathing/QcMobNavigator.h"

#include "server/pathing/QcOpeningSteering.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/pathing/QcPathSteps.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace QcMobNavigator {

namespace {

// Waypoints checked every tick ahead of the creature.
constexpr std::size_t checkedAhead = 2;
// A waypoint is not reached while the feet are this far below it (still climbing), or, on a ladder
// or above the top of one the path goes down, this far above it (still going down).
constexpr double climbSlack = 0.6;
constexpr double descendSlack = 0.9;
// Jumps only start this close to a higher waypoint.
constexpr double jumpDistance = 1.3;
// How far a floor may lie below the top of its cell (dirt paths, farmland, soul sand) and still
// count as the floor of the cell above: a creature on it is in that cell and does not jump to it.
constexpr double floorSink = 0.2;
// How far ahead safeStep looks for ground.
constexpr double stepLookAhead = 0.7;
// How far around the leading edge of a body wider than a block safeStep looks.
constexpr double edgeReach = 0.05;

double horizontalDistance(const QcVec3 &a, const QcVec3 &b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

// Cells a goal may move before its path is searched anew: a hunter follows its prey loosely, the
// more loosely the farther away it is (a quarter of the distance).
int tolerance(const QcCreature &mob)
{
    if (mob.path.purpose != QcMobPath::Purpose::Chase) {
        return 0;
    }
    return std::max(1, static_cast<int>(horizontalDistance(mob.motion.position, mob.path.goal) / 4.0));
}

int cellsApart(QcBlockPos a, QcBlockPos b)
{
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

QcPhysics::Steering towards(const QcCreature &mob, const QcVec3 &point, double speed)
{
    const QcVec3 delta{point.x - mob.motion.position.x, 0.0, point.z - mob.motion.position.z};
    if (delta.length() < 0.05) {
        return {{}, speed};
    }
    return {delta.normalized(), speed};
}

// On slippery ground (decision E-430) a creature brakes towards the end of its path instead of
// sliding past it and back again: its speed stays below the distance left times this share of the
// rate at which its velocity follows its wish (a critically damped approach).
constexpr double slideBrakeShare = 0.25;

// The speed for a creature `remaining` metres from the end of its path: `speed`, or less on slippery
// ground near the end.
double brakedSpeed(const QcCreature &mob, double speed, double remaining, const QcBlockAccess &world)
{
    if (!mob.motion.onGround) {
        return speed;
    }
    const double keep = QcPhysics::slideKeep(QcPhysics::surfaceUnder(world, mob.motion.position).slipperiness, QcPhysics::footDrag,
                                             QcPhysics::tickDuration);
    if (keep <= 0.0) {
        return speed;
    }
    return std::min(speed, remaining * (1.0 - keep) * QcPhysics::tickRate * slideBrakeShare);
}

// Whether the path goes on straight down from its waypoint `index` (a ladder).
bool downwards(const QcMobPath &path, std::size_t index)
{
    if (index + 1 >= path.cells.size()) {
        return false;
    }
    const QcBlockPos here = path.cells[index];
    const QcBlockPos next = path.cells[index + 1];
    return next.x == here.x && next.z == here.z && next.y < here.y;
}

// Passes the waypoints the creature reached and measures its progress towards the next; returns
// the point of the next waypoint (QcOpeningSteering::waypointPoint), none at the end of the path.
// A waypoint that lines the creature up for a door counts as reached only on the line, unless the
// creature made no progress for half of stuckTicks (pushed, knocked back, in a current).
std::optional<QcOpeningSteering::Point> follow(QcCreature &mob, const QcPathGround &ground)
{
    QcMobPath &path = mob.path;
    std::optional<QcOpeningSteering::Point> point;
    while (path.next < path.cells.size()) {
        const QcBlockPos waypoint = path.cells[path.next];
        point = QcOpeningSteering::waypointPoint(path, path.next, ground);
        const bool lined = point->onLane(mob.motion.position) || path.stuckTicks >= stuckTicks / 2;
        if (horizontalDistance(mob.motion.position, point->at) >= arriveDistance || !lined || mob.motion.position.y < waypoint.y - climbSlack
            || ((mob.motion.climbing || downwards(path, path.next)) && mob.motion.position.y > waypoint.y + descendSlack)) {
            break;
        }
        point.reset();
        ++path.next;
        path.closest = std::numeric_limits<double>::max();
        path.stuckTicks = 0;
    }

    if (point) {
        const double distance = horizontalDistance(mob.motion.position, point->at);
        if (distance < path.closest - 0.05) {
            path.closest = distance;
            path.stuckTicks = 0;
        } else {
            ++path.stuckTicks;
        }
    }
    return point;
}

bool needsSearch(const QcCreature &mob, const QcPathGround &ground)
{
    const QcMobPath &path = mob.path;
    if (path.queued) {
        return false;
    }
    if (!path.planned) {
        return true;
    }
    if (path.age < minReplanTicks) {
        return false;
    }

    // While it waits after a failed search, only a goal that moved far is searched anew.
    const int moved = cellsApart(QcPhysics::blockContaining(path.goal), path.plannedGoal);
    if (moved > (path.waitTicks > 0 ? std::max(farGoalMove, tolerance(mob)) : tolerance(mob))) {
        return true;
    }
    if (!pathClear(path, path.age % fullCheckTicks == 0, ground)) {
        return true;
    }
    if (path.waitTicks > 0) {
        return false;
    }
    return path.stuckTicks >= stuckTicks || (path.purpose == QcMobPath::Purpose::Chase && path.age >= chaseReplanTicks)
           || (path.finished() && !path.reachesGoal);
}

// The steering along a planned path: to the next waypoint (`point`, its point, from follow), then
// to the goal itself if the path gets there, as long as the step keeps to safe ground (the goal may
// lie beyond an edge, the path ends within its closeEnough).
QcPhysics::Steering along(const QcCreature &mob, double speed, const std::optional<QcOpeningSteering::Point> &point, const QcPathGround &ground)
{
    const QcMobPath &path = mob.path;
    if (!path.planned) {
        return {{}, speed};
    }
    if (path.next >= path.cells.size() || !point) {
        const double left = horizontalDistance(mob.motion.position, path.goal);
        const QcPhysics::Steering last =
            path.reachesGoal ? towards(mob, path.goal, brakedSpeed(mob, speed, left, ground.world)) : QcPhysics::Steering{{}, speed};
        return safeStep(mob, last.wish, ground) ? last : QcPhysics::Steering{{}, speed};
    }

    const QcBlockPos waypoint = path.cells[path.next];
    const QcOpeningSteering::Target target = QcOpeningSteering::target(mob, *point, ground);
    // The way left: to the waypoint, then about a metre per waypoint after it.
    const double left = horizontalDistance(mob.motion.position, point->at) + static_cast<double>(path.cells.size() - path.next - 1);
    QcPhysics::Steering steering = towards(mob, QcOpeningSteering::aimAt(mob, target), brakedSpeed(mob, speed, left, ground.world));
    steering = QcOpeningSteering::lineUp(mob, *point, steering);
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, floorSink, 0.0});
    steering.jump = waypoint.y > feet.y && horizontalDistance(mob.motion.position, point->at) < jumpDistance;

    // Down a ladder straight to the waypoint's level: pushing sideways on it would climb it.
    if (mob.motion.climbing && waypoint.y < feet.y) {
        steering.wish = {};
    }
    return steering;
}

// The closed door the creature stands in (another one shut it behind itself while this one was in
// the doorway), else the closed door on the next waypoint, if there is one.
std::optional<QcBlockPos> doorAhead(const QcCreature &mob, const QcPathGround &ground)
{
    const QcMobPath &path = mob.path;
    const QcPathDirectCells direct(ground.world, ground.cells);
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.1, 0.0});
    if (QcPathCell::kindOf(direct.cell(feet)) == QcPathCell::Kind::ClosedDoor) {
        return feet;
    }
    if (path.next >= path.cells.size()) {
        return std::nullopt;
    }
    const QcBlockPos waypoint = path.cells[path.next];
    if (QcPathCell::kindOf(direct.cell(waypoint)) != QcPathCell::Kind::ClosedDoor) {
        return std::nullopt;
    }
    return waypoint;
}

// Steering along the path; asks the planner for a path when needed only if it `mayPlan`.
Navigation navigate(QcCreature &mob, double speed, QcPathPlanner &planner, const QcPathGround &ground, bool mayPlan)
{
    QcMobPath &path = mob.path;
    if (!path.hasGoal()) {
        return {{{}, speed}, std::nullopt};
    }
    if (!planner.enabled()) {
        return {towards(mob, path.goal, speed), std::nullopt};
    }
    if (!mayPlan && !path.planned) {
        return {{{}, speed}, std::nullopt};
    }

    ++path.age;
    path.waitTicks = std::max(0, path.waitTicks - 1);
    const std::optional<QcOpeningSteering::Point> point = follow(mob, ground);
    if (mayPlan && needsSearch(mob, ground)) {
        planner.request(mob);
    }

    return {along(mob, speed, point, ground), ground.rules.opensDoors ? doorAhead(mob, ground) : std::nullopt};
}

} // namespace

void setGoal(QcCreature &mob, QcMobPath::Purpose purpose, const QcVec3 &goal, int closeEnough)
{
    // A path to a goal of another purpose is no path to this one, and its failures do not count.
    if (mob.path.purpose != purpose) {
        mob.path.clear();
        mob.path.failures = 0;
        mob.path.waitTicks = 0;
    }
    mob.path.purpose = purpose;
    mob.path.goal = goal;
    mob.path.closeEnough = closeEnough;
}

bool pathClear(const QcMobPath &path, bool all, const QcPathGround &ground)
{
    QcPathDirectCells direct(ground.world, ground.cells);
    const std::size_t end = all ? path.cells.size() : std::min(path.cells.size(), path.next + checkedAhead);
    for (std::size_t i = path.next; i < end; ++i) {
        if (!QcPathTerrain::standAt(direct, path.cells[i], ground.rules).ok) {
            return false;
        }
    }
    return true;
}

bool safeStep(const QcCreature &mob, const QcVec3 &direction, const QcPathGround &ground)
{
    const QcVec3 flat{direction.x, 0.0, direction.z};
    if (flat.length() < 1e-6) {
        return true;
    }
    // A body wider than a block (decision E-909): the column its leading edge moves into, as a
    // place for one column of the body (it overhangs the others).
    QcPathRules rules = ground.rules;
    QcVec3 from = mob.motion.position;
    QcVec3 to = mob.motion.position + flat.normalized() * stepLookAhead;
    if (rules.footprint() > 1) {
        const double half = static_cast<double>(rules.width) / 2.0;
        from = mob.motion.position + flat.normalized() * (half - edgeReach);
        to = mob.motion.position + flat.normalized() * (half + edgeReach);
        rules.width = 1.0f;
    }
    const QcBlockPos feet = QcPhysics::blockContaining(from + QcVec3{0.0, 0.01, 0.0});
    const QcBlockPos ahead = QcPhysics::blockContaining(to + QcVec3{0.0, 0.01, 0.0});
    if (ahead.x == feet.x && ahead.z == feet.z) {
        return true;
    }

    QcPathDirectCells direct(ground.world, ground.cells);
    for (int dy = 1; dy >= -std::max(static_cast<int>(rules.maxDrop), QcPathRules::waterDrop); --dy) {
        const QcBlockPos cell = ahead.offset(0, dy, 0);
        if (QcPathTerrain::standAt(direct, cell, rules).ok) {
            return QcPathTerrain::safeLanding(direct, cell, -dy, rules);
        }
        if (dy < 0 && !QcPathTerrain::passable(direct, cell, rules, false)) {
            return false;
        }
    }
    return false;
}

Navigation steer(QcCreature &mob, double speed, QcPathPlanner &planner, const QcBlockAccess &world, QcPathRules rules)
{
    return navigate(mob, speed, planner, {world, planner.cells(), rules}, true);
}

Navigation steerAlong(QcCreature &mob, double speed, QcPathPlanner &planner, const QcBlockAccess &world, QcPathRules rules)
{
    return navigate(mob, speed, planner, {world, planner.cells(), rules}, false);
}

} // namespace QcMobNavigator
