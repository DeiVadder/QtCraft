#include "server/mobs/QcCreatureBrains.h"

#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobSteering.h"
#include "server/pathing/QcMobNavigator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

QcPassiveBrain::QcPassiveBrain(QRandomGenerator &random, QcPathPlanner &paths)
    : m_random(random)
    , m_paths(paths)
{
}

QcPhysics::Steering QcPassiveBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    // Every creature looks once in sightCheckTicks, staggered by its id: one thinking every
    // `thinkTicks` (at most sightCheckTicks) looks at its thought in the first `thinkTicks` of them.
    const bool mayPlan = thinkTicks <= 1;
    const bool looks = (context.tick + mob.id) % QcMobSteering::sightCheckTicks < quint64(std::max(1, thinkTicks));
    const QcMobManager::PlayerView *target =
        chases() ? QcMobSteering::chooseTarget(mob, definition, *context.players, context.world, looks) : nullptr;
    // A hostile creature that has not looked yet still strikes a player right next to it.
    if (!target && QcMobSteering::strikeAdjacent(mob, definition, *context.players, context.world, *context.hits)) {
        mob.lastSpeed = definition.chaseSpeed;
        return {{}, definition.chaseSpeed};
    }

    const QcPhysics::Steering steering = target ? chase(mob, definition, *target, context, mayPlan) : wander(mob, definition, context, mayPlan);
    mob.lastSpeed = steering.speed;
    return steering;
}

QcMobNavigator::Navigation QcPassiveBrain::navigate(QcCreature &mob, double speed, const Context &context, bool mayPlan)
{
    return mayPlan ? QcMobNavigator::steer(mob, speed, m_paths, context.world, context.rules)
                   : QcMobNavigator::steerAlong(mob, speed, m_paths, context.world, context.rules);
}

QcPhysics::Steering QcPassiveBrain::chase(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                                          const Context &context, bool mayPlan)
{
    // In melee it stands and strikes; otherwise it walks its path, within one cell of the target.
    if (QcMobSteering::inMelee(mob, definition, target, context.world)) {
        QcMobSteering::strike(mob, definition, target, *context.hits);
        return {{}, definition.chaseSpeed};
    }

    QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, target.position, 1);
    const QcPhysics::Steering steering = navigate(mob, definition.chaseSpeed, context, mayPlan).steering;
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    const QcVec3 facing = steering.wish.length() > 0.0 ? steering.wish : towards;
    if (facing.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(facing);
    }
    return steering;
}

QcPhysics::Steering QcPassiveBrain::wander(QcCreature &mob, const QcMobDefinition &definition, const Context &context, bool mayPlan)
{
    QcMobPath &path = mob.path;
    double speed = definition.walkSpeed;
    mob.wanderTimer -= context.dt;
    if (mob.fleeTimer > 0.0) {
        // Away from the attacker; the search ends at the reachable place closest to the far point.
        if (path.purpose != QcMobPath::Purpose::Flee) {
            path.clear();
            QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Flee, mob.motion.position + mob.wanderDirection * fleeDistance, 0);
        }
        speed = definition.chaseSpeed;
    } else if (path.purpose == QcMobPath::Purpose::Flee || path.purpose == QcMobPath::Purpose::Chase) {
        path.clear();
        mob.wanderTimer = 0.0;
    }

    if (mob.fleeTimer <= 0.0 && mob.wanderTimer <= 0.0) {
        // Half of the time stand still, otherwise walk to a place nearby for a few seconds.
        mob.wanderTimer = 2.0 + m_random.bounded(5.0);
        path.clear();
        if (m_random.bounded(2) == 0) {
            mob.wanderDirection = {};
        } else {
            const double angle = m_random.bounded(2.0 * std::numbers::pi);
            mob.wanderDirection = {std::sin(angle), 0.0, -std::cos(angle)};
            QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, mob.motion.position + mob.wanderDirection * wanderDistance, 0);
        }
    }

    const QcPhysics::Steering steering = navigate(mob, speed, context, mayPlan).steering;
    // A wanderer at the end of its path stands until its time is up.
    if (path.purpose == QcMobPath::Purpose::Wander && path.finished()) {
        path.clear();
    }
    if (steering.wish.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(steering.wish);
    }
    return steering;
}
