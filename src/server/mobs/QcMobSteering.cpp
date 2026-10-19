#include "server/mobs/QcMobSteering.h"

#include "common/world/QcDayCycle.h"

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

std::optional<QcMobManager::PlayerHit> strike(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target)
{
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    if (towards.length() > 0.0) {
        mob.yaw = yawTowards(towards);
    }

    if (mob.attackCooldown <= 0.0) {
        mob.attackCooldown = definition.attackCooldown;
        mob.attackedThisTick = true;
        return QcMobManager::PlayerHit{
            .playerEntityId = target.entityId, .damage = definition.attackDamage, .knockback = towards.normalized(), .mobType = mob.type, .mob = mob.id};
    }
    return std::nullopt;
}

void provoke(QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players)
{
    if (!definition.neutralByDay) {
        return;
    }
    if (mob.struckBy != 0) {
        mob.provokedBy = mob.struckBy;
        mob.angerTimer = provokedSeconds;
        mob.struckBy = 0;
    }

    // The anger ends with its time, or when that player is gone or no target any more.
    const auto provoker = std::ranges::find(players, mob.provokedBy, &QcMobManager::PlayerView::entityId);
    if (mob.angerTimer <= 0.0 || provoker == players.end() || !provoker->alive || !provoker->targetable) {
        mob.provokedBy = 0;
        mob.angerTimer = 0.0;
    }
}

const QcMobManager::PlayerView *provoker(const QcCreature &mob, const QcMobDefinition &definition,
                                         const QList<QcMobManager::PlayerView> &players)
{
    if (!definition.neutralByDay || mob.provokedBy == 0) {
        return nullptr;
    }
    const auto it = std::ranges::find(players, mob.provokedBy, &QcMobManager::PlayerView::entityId);
    return it != players.end() ? &*it : nullptr;
}

bool calm(const QcCreature &mob, const QcMobDefinition &definition, double worldTime)
{
    return definition.neutralByDay && !QcDayCycle::isNight(worldTime) && !mob.underground && mob.provokedBy == 0;
}

bool leap(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target, QRandomGenerator &random)
{
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    const double distance = towards.length();
    const bool level = std::abs(target.position.y - mob.motion.position.y) <= 1.0;
    if (definition.leapSpeed <= 0.0 || !mob.motion.onGround || !level || distance < leapMin || distance > leapMax
        || random.generateDouble() >= leapChance) {
        return false;
    }
    mob.motion.velocity = towards.normalized() * definition.leapSpeed + QcVec3{0.0, leapLift, 0.0};
    mob.yaw = yawTowards(towards);
    return true;
}

Melee strikeAdjacent(QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players,
                     const QcBlockAccess &world)
{
    if (!definition.hostile || mob.calm || mob.attackCooldown > 0.0) {
        return {};
    }

    // The nearest player in melee: one behind glass does not shield another in reach.
    const QcMobManager::PlayerView *target = nullptr;
    double targetDistance = definition.attackRange;
    for (const QcMobManager::PlayerView &player : players) {
        const double distance = horizontalDistance(player.position, mob.motion.position);
        // A neutral creature strikes only the player that provoked it (decision E-460), an angry
        // creature neutral by day only the player that hurt it (decision E-451).
        const bool angry = definition.ai == QcMobDefinition::Ai::Neutral || (definition.neutralByDay && mob.provokedBy != 0);
        const bool provoked = !angry || player.entityId == mob.provokedBy;
        if (player.alive && player.targetable && provoked && distance <= targetDistance && inMelee(mob, definition, player, world)) {
            target = &player;
            targetDistance = distance;
        }
    }
    if (!target) {
        return {};
    }
    return {true, strike(mob, definition, *target)};
}

} // namespace QcMobSteering
