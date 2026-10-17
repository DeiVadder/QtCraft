#pragma once

#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"
#include "server/pathing/QcPathGround.h"

#include <optional>

class QcPathPlanner;

// Following a creature's path (decision E-400): it walks from waypoint to waypoint with the shared
// physics (jumping where the next waypoint is higher), checks that the next waypoints can still be
// stood on, notices when it makes no progress, and asks QcPathPlanner for a new path when the goal
// moved, the path is blocked or it got stuck, a chase path is old, or a partial path ended. After a
// path that did not get there it waits (QcMobPath::waitTicks) before it asks again, unless the path
// is blocked or the goal moved far (farGoalMove).
namespace QcMobNavigator {

// A chase path is searched again after this many ticks (the prey moves); no goal is searched again
// sooner than `minReplanTicks` after the last search.
inline constexpr int chaseReplanTicks = 100;
inline constexpr int minReplanTicks = 10;
// While a creature waits after a search that did not get there (QcMobPath::waitTicks), a goal that
// moved is searched anew only when it moved at least this many cells: an unreachable prey running
// about is not searched every minReplanTicks.
inline constexpr int farGoalMove = 8;
// Ticks without coming closer to the next waypoint until the creature counts as stuck.
inline constexpr int stuckTicks = 20;
// Horizontal distance from a waypoint's centre at which it counts as reached.
inline constexpr double arriveDistance = 0.35;
// Every this many ticks the whole rest of the path is checked, otherwise only the next waypoints.
inline constexpr int fullCheckTicks = 20;

// What to do this tick: the steering, and a closed door on the path or in the doorway the creature
// stands in (only for creatures that open doors, QcPathRules::opensDoors: villagers open it,
// QcVillagerBehaviour).
struct Navigation
{
    QcPhysics::Steering steering;
    std::optional<QcBlockPos> door;
};

// Gives the creature a goal; a goal in another cell than the planned one is searched anew, a goal
// of another purpose starts without a path and without failures.
void setGoal(QcCreature &mob, QcMobPath::Purpose purpose, const QcVec3 &goal, int closeEnough);

// Steering along the creature's path to its goal at `speed`; asks the planner for a path when
// needed. Without a goal the creature stands. With path finding switched off it walks straight at
// the goal.
Navigation steer(QcCreature &mob, double speed, QcPathPlanner &planner, const QcBlockAccess &world, QcPathRules rules);
// The same without asking for a path (a throttled creature in a crowd, decision E-401): it keeps
// following the path it has, or stands without one.
Navigation steerAlong(QcCreature &mob, double speed, QcPathPlanner &planner, const QcBlockAccess &world, QcPathRules rules);

// Whether the waypoints from the next one on (all of them, or the next two) can still be stood on.
bool pathClear(const QcMobPath &path, bool all, const QcPathGround &ground);

// Whether a step in `direction` keeps the creature on ground it can stand on: the cell ahead is
// level, a step up or a drop of at most QcPathRules::maxDrop or into deep water (pushes between
// creatures and coasting in a crowd must not take one off a cliff).
bool safeStep(const QcCreature &mob, const QcVec3 &direction, const QcPathGround &ground);

} // namespace QcMobNavigator
