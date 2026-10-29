// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

Towards State::towards(const QcCreature &mob, QcBlockPos target, const Routes &routes, Budget budget)
{
    const Way way = wayOutOfWater(mob, routes, budget);
    const int asked = way.asked ? 1 : 0;
    if (way.shore) {
        return {*way.shore, asked};
    }
    budget.used += asked;

    const QcBlockPos feet = feetOf(mob);
    if (std::abs(target.x - feet.x) <= routes.reach() && std::abs(target.z - feet.z) <= routes.reach()) {
        leg.reset();
        return {target, asked};
    }
    const bool walking = leg && horizontalDistanceTo(mob, *leg) > arrived
                         && !(mob.path.finished() && mob.path.plannedGoal == *leg);
    retryIn = std::max(0, retryIn - 1);
    if (walking || retryIn > 0 || !budget.take()) {
        return {leg.value_or(target), asked};
    }

    leg = routes.nextLeg(feet, target);
    retryIn = leg ? 0 : retryTicks;
    return {leg.value_or(target), asked + 1};
}

Way State::wayOutOfWater(const QcCreature &mob, const Routes &routes, Budget budget)
{
    if (!mob.motion.inWater) {
        // Ashore once it stands on dry ground (at the surface it bobs in and out of the water).
        if (mob.motion.onGround) {
            shore.reset();
        }
        return {shore, false};
    }
    const bool noWay = mob.path.finished() && !mob.path.reachesGoal && !mob.path.queued;
    if (shore || !noWay || budget.spent()) {
        return {shore, false};
    }

    shore = routes.wayOutOfWater(feetOf(mob));
    leg.reset();
    return {shore, true};
}

} // namespace QcMobLegs
