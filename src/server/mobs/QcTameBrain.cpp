// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcTameBrain.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcSafeSpot.h"
#include "server/mobs/QcMobSteering.h"
#include "server/mobs/QcMobTeleport.h"
#include "server/pathing/QcMobNavigator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

double horizontal(const QcVec3 &a, const QcVec3 &b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

void face(QcCreature &mob, const QcVec3 &point)
{
    const QcVec3 towards{point.x - mob.motion.position.x, 0.0, point.z - mob.motion.position.z};
    if (towards.length() > 1e-6) {
        mob.yaw = QcMobSteering::yawTowards(towards);
    }
}

} // namespace

QcTameBrain::QcTameBrain(QRandomGenerator &random, QcPathPlanner &paths, QcMobBrain &wild)
    : QcPassiveBrain(random, paths)
    , m_wild(wild)
{
}

bool QcTameBrain::teleportNear(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, QRandomGenerator &random,
                               const QcVec3 &owner)
{
    const int range = static_cast<int>(teleportRange);
    const int cells = QcSafeSpot::cellsOf(definition.height);
    const QcBlockPos centre = QcPhysics::blockContaining(owner);
    for (int attempt = 0; attempt < teleportAttempts; ++attempt) {
        const int dx = random.bounded(-range, range + 1);
        const int dz = random.bounded(-range, range + 1);
        const QcBlockPos feet = centre.offset(dx, random.bounded(-1, 2), dz);
        if ((std::abs(dx) > 1 || std::abs(dz) > 1) && QcSafeSpot::canStand(world, feet, cells)) {
            QcMobTeleport::land(mob, {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5});
            return true;
        }
    }
    return false;
}

const QcMobManager::PlayerView *QcTameBrain::ownerOf(const QcTameState &tame, const Context &context)
{
    if (!context.players) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(*context.players, [&](const QcMobManager::PlayerView &player) {
        return player.alive && player.identity == tame.owner;
    });
    return it == context.players->end() ? nullptr : &*it;
}

QcPhysics::Steering QcTameBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    if (!mob.isTame() || !definition.tameable) {
        mob.pose = QcPose::Standing;
        return m_wild.think(mob, definition, context, thinkTicks);
    }

    // A pet is never angry at a player nor runs from one (no player fights players in this game).
    QcTameState &tame = *mob.tame;
    mob.provokedBy = 0;
    mob.angerTimer = 0.0;
    mob.struckBy = 0;
    mob.fleeTimer = 0.0;
    mob.chasing = 0;
    mob.lastSpeed = 0.0;
    if (tame.sitting) {
        mob.pose = QcPose::Sitting;
        mob.path.clear();
        tame.target = 0;
        return {};
    }

    mob.pose = QcPose::Standing;
    const bool mayPlan = thinkTicks <= 1;
    const QcMobManager::PlayerView *owner = ownerOf(tame, context);
    if (definition.tameable->defends) {
        const Target chosen = chosenTarget(mob, tame, owner, context, context.dt * thinkTicks);
        tame.target = chosen.id;
        tame.targetTick = chosen.tick;
        tame.targetTimer = chosen.timer;
        if (const QcCreature *target = validTarget(mob, tame.target, context)) {
            const QcPhysics::Steering steering = attack(mob, definition, *target, context, mayPlan);
            mob.lastSpeed = steering.speed;
            return steering;
        }
    }
    if (!owner) {
        mob.path.clear();
        return {};
    }

    const QcPhysics::Steering steering = keepNear(mob, definition, *owner, context, mayPlan);
    mob.lastSpeed = steering.speed;
    return steering;
}

QcTameBrain::Target QcTameBrain::chosenTarget(const QcCreature &mob, const QcTameState &tame, const QcMobManager::PlayerView *owner,
                                              const Context &context, double seconds) const
{
    // A valid target is kept until the owner fights anew: then the newest fight wins (what hurt the
    // owner or what they struck). Without one the pet takes up the newest foe it may attack.
    Target target{tame.target, tame.targetTick, tame.targetTimer};
    if (owner) {
        const bool keeps = validTarget(mob, target.id, context) != nullptr;
        const std::array<std::pair<quint32, quint64>, 2> fights{{{owner->hurtBy, owner->hurtTick}, {owner->struck, owner->struckTick}}};
        const std::pair<quint32, quint64> *chosen = nullptr;
        for (const auto &fight : fights) {
            const bool newer = !keeps || fight.second > target.tick;
            if (fight.first != 0 && fight.first != target.id && newer && (!chosen || fight.second > chosen->second)
                && validTarget(mob, fight.first, context)) {
                chosen = &fight;
            }
        }
        if (chosen) {
            target = {chosen->first, chosen->second, targetSeconds};
        }
    }

    target.timer = std::max(0.0, target.timer - seconds);
    if (target.id != 0 && (target.timer <= 0.0 || !validTarget(mob, target.id, context))) {
        target.id = 0;
        target.timer = 0.0;
    }
    return target;
}

const QcCreature *QcTameBrain::validTarget(const QcCreature &mob, quint32 id, const Context &context)
{
    if (id == 0 || id == mob.id) {
        return nullptr;
    }
    const auto it = std::ranges::find(context.mobs, id, &QcCreature::id);
    if (it == context.mobs.end() || it->dormant || it->isTame()
        || (it->motion.position - mob.motion.position).length() > targetRange) {
        return nullptr;
    }
    return &*it;
}

QcPhysics::Steering QcTameBrain::attack(QcCreature &mob, const QcMobDefinition &definition, const QcCreature &target, const Context &context,
                                        bool mayPlan)
{
    const QcMobDefinition &kind = context.world.content().mobs()[target.type];
    const QcVec3 eye = mob.motion.position + QcVec3{0.0, definition.height * QcMobSteering::eyeShare, 0.0};
    const bool close = horizontal(mob.motion.position, target.motion.position) <= definition.attackRange + kind.width * 0.5
                       && std::abs(target.motion.position.y - mob.motion.position.y) < std::max(definition.height, kind.height) + 0.5;
    if (close && QcLineOfSight::reaches(context.world, eye, QcPhysics::boxAt(target.motion.position, kind.width, kind.height))) {
        face(mob, target.motion.position);
        if (mob.attackCooldown <= 0.0 && context.creatureHits) {
            mob.attackCooldown = definition.attackCooldown;
            mob.attackedThisTick = true;
            context.creatureHits->append({.attacker = mob.id, .target = target.id, .damage = definition.attackDamage, .from = mob.motion.position});
        }
        return {{}, definition.chaseSpeed};
    }

    QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, target.motion.position, 1);
    const QcPhysics::Steering steering = navigate(mob, definition.chaseSpeed, context, mayPlan).steering;
    face(mob, steering.wish.length() > 0.0 ? mob.motion.position + steering.wish : target.motion.position);
    return steering;
}

QcPhysics::Steering QcTameBrain::keepNear(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &owner,
                                          const Context &context, bool mayPlan)
{
    const QcTameDefinition &tame = *definition.tameable; // NOLINT(bugprone-unchecked-optional-access): think() checked it
    const double distance = (owner.position - mob.motion.position).length();
    if (distance > tame.teleportDistance && !mob.confined) {
        if (teleportNear(mob, definition, context.world, random(), owner.position)) {
            face(mob, owner.position);
            return {};
        }
    }

    // It starts to follow beyond the follow distance and keeps on until it is close.
    const bool following = mob.path.purpose == QcMobPath::Purpose::Chase;
    if (distance > tame.followDistance || (following && distance > tame.stopDistance)) {
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, owner.position, 1);
        const QcPhysics::Steering steering = navigate(mob, definition.chaseSpeed, context, mayPlan).steering;
        face(mob, steering.wish.length() > 0.0 ? mob.motion.position + steering.wish : owner.position);
        return steering;
    }

    mob.path.clear();
    face(mob, owner.position);
    return {};
}
