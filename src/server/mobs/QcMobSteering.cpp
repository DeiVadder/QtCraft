#include "server/mobs/QcMobSteering.h"

#include "server/gameplay/QcLineOfSight.h"

#include <QtMath>

#include <algorithm>
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

namespace {

bool isValidTarget(const QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &player)
{
    return player.alive && player.targetable && (player.position - mob.motion.position).length() < definition.sightRange;
}

// The nearest valid target the creature sees from its eyes; nullptr when it sees none.
const QcMobManager::PlayerView *nearestSeen(const QcCreature &mob, const QcMobDefinition &definition,
                                            const QList<QcMobManager::PlayerView> &players, const QcBlockAccess &world)
{
    const QcVec3 eye = mob.motion.position + QcVec3{0.0, definition.height * eyeShare, 0.0};
    const QcMobManager::PlayerView *seen = nullptr;
    double seenDistance = definition.sightRange;
    for (const QcMobManager::PlayerView &player : players) {
        const double distance = (player.position - mob.motion.position).length();
        if (!isValidTarget(mob, definition, player) || distance >= seenDistance) {
            continue;
        }
        if (QcLineOfSight::seesPoint(world, eye, player.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0})) {
            seen = &player;
            seenDistance = distance;
        }
    }
    return seen;
}

} // namespace

const QcMobManager::PlayerView *chooseTarget(QcCreature &mob, const QcMobDefinition &definition,
                                             const QList<QcMobManager::PlayerView> &players, const QcBlockAccess &world,
                                             bool looks)
{
    if (looks) {
        if (const QcMobManager::PlayerView *seen = nearestSeen(mob, definition, players, world)) {
            mob.chasing = seen->entityId;
            mob.targetMemory = targetMemorySeconds;
            return seen;
        }
    }

    // The remembered target, while it is still one.
    const auto current = std::ranges::find(players, mob.chasing, &QcMobManager::PlayerView::entityId);
    if (mob.chasing != 0 && mob.targetMemory > 0.0 && current != players.end() && isValidTarget(mob, definition, *current)) {
        return &*current;
    }
    mob.chasing = 0;
    mob.targetMemory = 0.0;
    return nullptr;
}

bool inMelee(const QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
             const QcBlockAccess &world)
{
    if (horizontalDistance(target.position, mob.motion.position) > definition.attackRange
        || std::abs(target.position.y - mob.motion.position.y) > verticalReach) {
        return false;
    }

    const QcVec3 eye = mob.motion.position + QcVec3{0.0, definition.height * eyeShare, 0.0};
    return QcLineOfSight::reaches(world, eye, QcPhysics::boxAt(target.position, QcPhysics::playerWidth, QcPhysics::playerHeight));
}

void strike(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
            QList<QcMobManager::PlayerHit> &hits)
{
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    if (towards.length() > 0.0) {
        mob.yaw = yawTowards(towards);
    }

    if (mob.attackCooldown <= 0.0) {
        mob.attackCooldown = definition.attackCooldown;
        hits.append({target.entityId, definition.attackDamage, towards.normalized()});
        mob.attackedThisTick = true;
    }
}

bool strikeAdjacent(QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players,
                    const QcBlockAccess &world, QList<QcMobManager::PlayerHit> &hits)
{
    if (!definition.hostile || mob.attackCooldown > 0.0) {
        return false;
    }

    // The nearest player in melee: one behind glass does not shield another in reach.
    const QcMobManager::PlayerView *target = nullptr;
    double targetDistance = definition.attackRange;
    for (const QcMobManager::PlayerView &player : players) {
        const double distance = horizontalDistance(player.position, mob.motion.position);
        // A neutral creature strikes only the player that provoked it (decision E-460).
        const bool provoked = definition.ai != QcMobDefinition::Ai::Neutral || player.entityId == mob.provokedBy;
        if (player.alive && player.targetable && provoked && distance <= targetDistance && inMelee(mob, definition, player, world)) {
            target = &player;
            targetDistance = distance;
        }
    }
    if (!target) {
        return false;
    }
    strike(mob, definition, *target, hits);
    return true;
}

} // namespace QcMobSteering
