#include "QcMobSteering.h"

#include <QtMath>

#include <cmath>

namespace QcMobSteering {

namespace {

double horizontalDistance(const QcVec3 &a, const QcVec3 &b)
{
    const double dx = a.x - b.x;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

float yawTowards(const QcVec3 &direction)
{
    return float(qRadiansToDegrees(std::atan2(direction.x, -direction.z)));
}

const QcMobManager::PlayerView *nearestTarget(const QcMobManager::Mob &mob, const QcMobDefinition &definition,
                                              const QList<QcMobManager::PlayerView> &players)
{
    const QcMobManager::PlayerView *target = nullptr;
    double targetDistance = definition.sightRange;
    for (const QcMobManager::PlayerView &player : players) {
        const double distance = (player.position - mob.motion.position).length();
        if (player.alive && player.targetable && distance < targetDistance) {
            target = &player;
            targetDistance = distance;
        }
    }
    return target;
}

QcPhysics::Steering chase(QcMobManager::Mob &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                          QList<QcMobManager::PlayerHit> &hits)
{
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    QcPhysics::Steering steering{towards.normalized(), definition.chaseSpeed};
    const bool inReach = horizontalDistance(target.position, mob.motion.position) <= definition.attackRange
                         && std::abs(target.position.y - mob.motion.position.y) <= verticalReach;

    if (inReach) {
        steering.wish = {};
        if (mob.attackCooldown <= 0.0) {
            mob.attackCooldown = definition.attackCooldown;
            hits.append({target.entityId, definition.attackDamage, towards.normalized()});
            mob.attackedThisTick = true;
        }
    }

    if (towards.length() > 0.0) {
        mob.yaw = yawTowards(towards);
    }

    return steering;
}

} // namespace QcMobSteering
