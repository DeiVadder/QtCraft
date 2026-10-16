#include "QcMobNavigator.h"

#include "QcPathPlanner.h"

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

QcVec3 centreOf(QcBlockPos cell)
{
    return {cell.x + 0.5, double(cell.y), cell.z + 0.5};
}

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
    return std::max(1, int(horizontalDistance(mob.motion.position, mob.path.goal) / 4.0));
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

// Passes the waypoints the creature reached and measures its progress towards the next.
void follow(QcCreature &mob)
{
    QcMobPath &path = mob.path;
    while (path.next < path.cells.size()) {
        const QcBlockPos waypoint = path.cells[path.next];
        if (horizontalDistance(mob.motion.position, centreOf(waypoint)) >= arriveDistance
            || mob.motion.position.y < waypoint.y - climbSlack || ((mob.motion.climbing || downwards(path, path.next)) && mob.motion.position.y > waypoint.y + descendSlack)) {
            break;
        }
        ++path.next;
        path.closest = std::numeric_limits<double>::max();
        path.stuckTicks = 0;
    }

    if (path.next < path.cells.size()) {
        const double distance = horizontalDistance(mob.motion.position, centreOf(path.cells[path.next]));
        if (distance < path.closest - 0.05) {
            path.closest = distance;
            path.stuckTicks = 0;
        } else {
            ++path.stuckTicks;
        }
    }
}

bool needsSearch(const QcCreature &mob, const QcPathPlanner &planner, const QcBlockAccess &world, QcPathRules rules)
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
    if (!pathClear(path, path.age % fullCheckTicks == 0, world, planner.cells(), rules)) {
        return true;
    }
    if (path.waitTicks > 0) {
        return false;
    }
    return path.stuckTicks >= stuckTicks || (path.purpose == QcMobPath::Purpose::Chase && path.age >= chaseReplanTicks)
           || (path.finished() && !path.reachesGoal);
}

// What one steering looks at, and whether it may ask for a path.
struct Context
{
    QcPathPlanner &planner;
    const QcBlockAccess &world;
    QcPathRules rules;
    bool mayPlan = true;
};

// The steering along a planned path: to the next waypoint, then to the goal itself if the path gets
// there, as long as the step keeps to safe ground (the goal may lie beyond an edge, the path ends
// within its closeEnough).
QcPhysics::Steering along(const QcCreature &mob, double speed, const Context &context)
{
    const QcMobPath &path = mob.path;
    if (!path.planned) {
        return {{}, speed};
    }
    if (path.next >= path.cells.size()) {
        const QcPhysics::Steering last = path.reachesGoal ? towards(mob, path.goal, speed) : QcPhysics::Steering{{}, speed};
        return safeStep(mob, last.wish, context.world, context.planner.cells(), context.rules) ? last : QcPhysics::Steering{{}, speed};
    }

    const QcBlockPos waypoint = path.cells[path.next];
    QcPhysics::Steering steering = towards(mob, centreOf(waypoint), speed);
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, floorSink, 0.0});
    steering.jump = waypoint.y > feet.y && horizontalDistance(mob.motion.position, centreOf(waypoint)) < jumpDistance;

    // Down a ladder straight to the waypoint's level: pushing sideways on it would climb it.
    if (mob.motion.climbing && waypoint.y < feet.y) {
        steering.wish = {};
    }
    return steering;
}

// The closed door on the next waypoint, if there is one.
std::optional<QcBlockPos> doorAhead(const QcMobPath &path, const QcBlockAccess &world, const QcPathCellTable &cells)
{
    if (path.next >= path.cells.size()) {
        return std::nullopt;
    }
    const QcPathDirectCells direct(world, cells);
    const QcBlockPos waypoint = path.cells[path.next];
    if (QcPathCell::kindOf(direct.cell(waypoint)) != QcPathCell::Kind::ClosedDoor) {
        return std::nullopt;
    }
    return waypoint;
}

Navigation navigate(QcCreature &mob, double speed, const Context &context)
{
    QcMobPath &path = mob.path;
    if (!path.hasGoal()) {
        return {{{}, speed}, std::nullopt};
    }
    if (!context.planner.enabled()) {
        return {towards(mob, path.goal, speed), std::nullopt};
    }
    if (!context.mayPlan && !path.planned) {
        return {{{}, speed}, std::nullopt};
    }

    ++path.age;
    path.waitTicks = std::max(0, path.waitTicks - 1);
    follow(mob);
    if (context.mayPlan && needsSearch(mob, context.planner, context.world, context.rules)) {
        context.planner.request(mob);
    }

    return {along(mob, speed, context), context.rules.opensDoors ? doorAhead(path, context.world, context.planner.cells()) : std::nullopt};
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

bool pathClear(const QcMobPath &path, bool all, const QcBlockAccess &world, const QcPathCellTable &cells, QcPathRules rules)
{
    QcPathDirectCells direct(world, cells);
    const std::size_t end = all ? path.cells.size() : std::min(path.cells.size(), path.next + checkedAhead);
    for (std::size_t i = path.next; i < end; ++i) {
        if (!QcPathTerrain::standAt(direct, path.cells[i], rules).ok) {
            return false;
        }
    }
    return true;
}

bool safeStep(const QcCreature &mob, const QcVec3 &direction, const QcBlockAccess &world, const QcPathCellTable &cells,
              QcPathRules rules)
{
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.01, 0.0});
    const QcVec3 flat{direction.x, 0.0, direction.z};
    if (flat.length() < 1e-6) {
        return true;
    }
    const QcBlockPos ahead = QcPhysics::blockContaining(mob.motion.position + flat.normalized() * stepLookAhead + QcVec3{0.0, 0.01, 0.0});
    if (ahead.x == feet.x && ahead.z == feet.z) {
        return true;
    }

    QcPathDirectCells direct(world, cells);
    for (int dy = 1; dy >= -std::max(rules.maxDrop, QcPathRules::waterDrop); --dy) {
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
    return navigate(mob, speed, {planner, world, rules, true});
}

Navigation steerAlong(QcCreature &mob, double speed, QcPathPlanner &planner, const QcBlockAccess &world, QcPathRules rules)
{
    return navigate(mob, speed, {planner, world, rules, false});
}

} // namespace QcMobNavigator
