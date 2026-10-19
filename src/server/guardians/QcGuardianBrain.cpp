#include "server/guardians/QcGuardianBrain.h"

#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobSteering.h"
#include "server/pathing/QcMobNavigator.h"

#include <algorithm>

QcPhysics::Steering QcGuardianBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    if (!mob.guards) {
        return QcHostileBrain::think(mob, definition, context, thinkTicks);
    }

    // It looks once in sightCheckTicks like hostile creatures, among the players within its leash
    // only: the nearest of them is its target, one who left the leash is let go.
    const bool mayPlan = thinkTicks <= 1;
    const bool looks = (context.tick + mob.id) % QcMobSteering::sightCheckTicks < quint64(std::max(1, thinkTicks));
    QList<QcMobManager::PlayerView> leashed;
    for (const QcMobManager::PlayerView &player : *context.players) {
        if (mob.guards->distanceTo(player.position) <= definition.guardLeash) {
            leashed.append(player);
        }
    }
    const QcMobManager::PlayerView *target = QcMobSteering::chooseTarget(mob, definition, leashed, context.world, looks);
    if (!target) {
        mob.chasing = 0;
        mob.targetMemory = 0.0;
    }
    if (target) {
        const QcPhysics::Steering steering = chase(mob, definition, *target, context, mayPlan);
        mob.lastSpeed = steering.speed;
        return steering;
    }

    keepToZone(mob, *mob.guards, definition.walkSpeed);
    const QcPhysics::Steering steering = wander(mob, definition, context, mayPlan);
    mob.lastSpeed = steering.speed;
    return steering;
}

void QcGuardianBrain::keepToZone(QcCreature &mob, const QcGuardPost &post, double walkSpeed)
{
    const QcMobPath &path = mob.path;
    const bool strolling = path.purpose == QcMobPath::Purpose::Wander && !path.finished();
    const bool leaves = strolling && post.distanceTo(path.goal) > 0.0;
    const bool outside = !strolling && post.distanceTo(mob.motion.position) > homeSlack;
    if (leaves || outside) {
        const QcVec3 goal{post.min.x + random().bounded(double(post.max.x - post.min.x + 1)), double(post.min.y),
                          post.min.z + random().bounded(double(post.max.z - post.min.z + 1))};
        const QcVec3 to = outside ? post.centre() : goal;
        mob.path.clear();
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, to, 1);
        // Long enough to walk there: the next stroll does not cut the way back short.
        mob.wanderTimer = std::max(mob.wanderTimer, 2.0 + (to - mob.motion.position).length() / std::max(0.1, walkSpeed));
    }
}
