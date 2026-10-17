#include "server/pathing/QcMobLegs.h"

#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace QcMobLegs {

namespace {

QcBlockPos feetOf(const QcCreature &mob)
{
    return QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.1, 0.0});
}

double horizontalDistanceTo(const QcCreature &mob, QcBlockPos cell)
{
    return std::hypot(mob.motion.position.x - (cell.x + 0.5), mob.motion.position.z - (cell.z + 0.5));
}

} // namespace

QcBlockPos towards(const QcCreature &mob, State &state, QcBlockPos target, const Routes &routes, Budget &budget)
{
    if (const std::optional<QcBlockPos> shore = wayOutOfWater(mob, state, routes, budget)) {
        return *shore;
    }
    const QcBlockPos feet = feetOf(mob);
    if (std::abs(target.x - feet.x) <= routes.reach() && std::abs(target.z - feet.z) <= routes.reach()) {
        state.leg.reset();
        return target;
    }
    const bool walking = state.leg && horizontalDistanceTo(mob, *state.leg) > arrived
                         && !(mob.path.finished() && mob.path.plannedGoal == *state.leg);
    state.retryIn = std::max(0, state.retryIn - 1);
    if (walking || state.retryIn > 0 || budget.spent()) {
        return state.leg.value_or(target);
    }

    ++budget.used;
    state.leg = routes.nextLeg(feet, target);
    state.retryIn = state.leg ? 0 : retryTicks;
    return state.leg.value_or(target);
}

std::optional<QcBlockPos> wayOutOfWater(const QcCreature &mob, State &state, const Routes &routes, Budget &budget)
{
    if (!mob.motion.inWater) {
        // Ashore once it stands on dry ground (at the surface it bobs in and out of the water).
        if (mob.motion.onGround) {
            state.shore.reset();
        }
        return state.shore;
    }
    const bool noWay = mob.path.finished() && !mob.path.reachesGoal && !mob.path.queued;
    if (state.shore || !noWay || budget.spent()) {
        return state.shore;
    }

    ++budget.used;
    state.shore = routes.wayOutOfWater(feetOf(mob));
    state.leg.reset();
    return state.shore;
}

} // namespace QcMobLegs
