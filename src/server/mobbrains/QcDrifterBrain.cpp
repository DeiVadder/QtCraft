// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobbrains/QcDrifterBrain.h"

#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/mobbrains/QcArcherBrain.h"
#include "server/mobbrains/QcMobSteering.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// What a sum of tick lengths may fall short of a time and still have reached it.
constexpr double chargeSlack = 1e-9;

const QcMobManager::PlayerView *riderOf(const QcCreature &mob, const QList<QcMobManager::PlayerView> &players)
{
    const auto it = std::ranges::find(players, mob.rider, &QcMobManager::PlayerView::entityId);
    return mob.rider != 0 && it != players.end() ? &*it : nullptr;
}

QcVec3 middleOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
}

// Whether something solid lies within a drop of the feet `ahead` metres along `heading`.
bool groundAhead(const QcCreature &mob, const QcVec3 &heading, double ahead, const QcServerWorld &world)
{
    const QcBlockPos cell = QcPhysics::blockContaining(mob.motion.position + heading * ahead);
    for (int down = 1; down <= QcDriftDefinition::dropReach + 1; ++down) {
        if (!QcPhysics::collisionBoxesAt(world, cell.offset(0, -down, 0)).isEmpty()) {
            return true;
        }
    }
    return false;
}

// Whether the cell below the feet `ahead` metres along `heading` is lava.
bool lavaAhead(const QcCreature &mob, const QcVec3 &heading, double ahead, const QcServerWorld &world)
{
    const QcBlockPos cell = QcPhysics::blockContaining(mob.motion.position + heading * ahead).offset(0, -1, 0);
    const std::optional<QcBlockId> block = world.blockAt(cell);
    return block && world.content().block(*block).isLava();
}

} // namespace

QcDrifterBrain::QcDrifterBrain(QRandomGenerator &random)
    : m_random(random)
{
}

QcPhysics::Steering QcDrifterBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    const double seconds = context.dt * thinkTicks;
    mob.drifter->feetY.reset();
    const QcMobManager::PlayerView *rider = riderOf(mob, *context.players);
    if (rider && definition.ride && rider->held != qcNoItem && rider->held == definition.ride->steerId) {
        const QcVec3 flat{rider->look.x, 0.0, rider->look.z};
        mob.lastSpeed = definition.ride->speed;
        return {flat.length() > 1e-6 ? flat.normalized() : QcVec3{}, definition.ride->speed};
    }
    // A confined one (a showcase's pen) looks but does not shoot its walls down.
    if (definition.hostile && !mob.confined) {
        const bool looks = (context.tick + mob.id) % QcMobSteering::sightCheckTicks < static_cast<quint64>(std::max(1, thinkTicks));
        const QcMobManager::PlayerView *target = QcMobSteering::chooseTarget(mob, definition, *context.players, context.world, looks);
        mob.chasing = target ? target->entityId : 0;
        if (target) {
            mob.yaw = QcMobSteering::yawTowards(target->position - mob.motion.position);
            if (definition.hover && definition.hover->risesToTarget) {
                return holdAt(mob, definition, *target, context, seconds);
            }
            charge(mob, definition, *target, context, seconds);
        } else {
            mob.drifter->stopCharge();
        }
    }
    return drift(mob, definition, context, seconds);
}

QcPhysics::Steering QcDrifterBrain::drift(QcCreature &mob, const QcMobDefinition &definition, const Context &context, double seconds)
{
    QcDrifterState &state = *mob.drifter;
    state.headingSeconds -= seconds;
    if (state.headingSeconds <= 0.0 || mob.blockedLastTick) {
        // One that rests does so after each heading; a wall ends the heading and the rest begins.
        state.resting = definition.drift.restSeconds > 0.0 && !state.resting;
        state.heading = state.resting ? state.heading : newHeading(mob, definition, context);
        state.headingSeconds = (state.resting ? definition.drift.restSeconds : definition.drift.seconds) * (0.5 + m_random.generateDouble());
    }

    // One that keeps to lava waits at the shore for a heading that leads back over it, and one
    // that keeps to the ground does not walk off an edge.
    const bool atShore = definition.drift.keepsToLava && !lavaAhead(mob, state.heading, 1.0, context.world)
                         && lavaAhead(mob, {}, 0.0, context.world);
    const bool atEdge = definition.drift.keepsToGround && !groundAhead(mob, state.heading, 1.0, context.world)
                        && groundAhead(mob, {}, 0.0, context.world);
    if (atEdge && !state.resting) {
        state.headingSeconds = 0.0;
    }
    const bool stops = atShore || atEdge || state.resting;
    mob.lastSpeed = stops ? 0.0 : definition.walkSpeed;
    return {stops ? QcVec3{} : state.heading, mob.lastSpeed};
}

QcPhysics::Steering QcDrifterBrain::holdAt(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                                           const Context &context, double seconds)
{
    QcDrifterState &state = *mob.drifter;
    state.offsetSeconds -= seconds;
    if (state.offsetSeconds <= 0.0) {
        state.offsetSeconds = heightOffsetSeconds;
        state.heightOffset = heightOffsetMean + heightOffsetDeviation * normalDraw();
    }
    const double eyes = target.position.y + QcPhysics::playerEyeHeight;
    state.feetY = eyes - state.heightOffset - definition.height * QcMobSteering::eyeShare;

    // Close by it strikes and presses on; farther off it stays where it is and fires.
    if (!QcMobSteering::inMelee(mob, definition, target, context.world)) {
        charge(mob, definition, target, context, seconds);
        mob.lastSpeed = 0.0;
        return {{}, 0.0};
    }
    state.stopCharge();
    if (const std::optional<QcMobManager::PlayerHit> hit = QcMobSteering::strike(mob, definition, target)) {
        context.hits->append(*hit);
    }
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    mob.lastSpeed = definition.chaseSpeed;
    return {towards.length() > 1e-6 ? towards.normalized() : QcVec3{}, mob.lastSpeed};
}

QcVec3 QcDrifterBrain::newHeading(const QcCreature &mob, const QcMobDefinition &definition, const Context &context)
{
    QcVec3 heading;
    for (int tries = 0; tries < headingTries; ++tries) {
        const double angle = m_random.bounded(2.0 * std::numbers::pi);
        heading = {std::cos(angle), 0.0, std::sin(angle)};
        if (!definition.drift.keepsToLava || lavaAhead(mob, heading, lavaLookAhead, context.world)) {
            break;
        }
    }
    return heading;
}

void QcDrifterBrain::charge(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                            const Context &context, double seconds)
{
    const QcArcherDefinition &archer = definition.archer;
    const QcVec3 eyes = target.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 middle = middleOf(mob, definition);
    QcDrifterState &state = *mob.drifter;
    if (mob.attackCooldown > 0.0 || (eyes - middle).length() > archer.range) {
        state.stopCharge();
        return;
    }
    // A charge begins only at a target it sees, and with its warning: the cry comes drawSeconds
    // before the shot.
    if (state.chargeSeconds == 0.0) {
        if (!QcLineOfSight::seesPoint(context.world, middle, eyes)) {
            return;
        }
        state.warns = true;
    }
    state.chargeSeconds += seconds;
    state.flares = archer.flares;
    if (state.chargeSeconds + chargeSlack < archer.drawSeconds || !context.launches) {
        return;
    }

    if (!QcLineOfSight::seesPoint(context.world, middle, eyes)) {
        state.stopCharge();
        return;
    }
    const QcVec3 towards = (eyes - middle).normalized();
    const double muzzle = definition.width * 0.5 + 0.8;
    const QcVec3 from = middle + towards * muzzle;

    // A target closer than the muzzle's distance has no aim of its own: from the muzzle it lies
    // behind the shot or right at it.
    const QcVec3 aim = eyes - from;
    const bool straight = archer.scatter <= 0.0 || aim.length() < muzzle;
    context.launches->append({mob.id, archer.projectile, from, shotAt(archer, straight ? towards : aim), QcProjectilePickup::Never, 1.0});

    // The next shot of a volley comes volleySeconds later; after the last it waits.
    state.shotsFired += 1;
    if (state.shotsFired < archer.volley) {
        state.chargeSeconds = archer.drawSeconds - archer.volleySeconds;
        return;
    }
    state.stopCharge();
    mob.attackCooldown = archer.cooldownSeconds;
}

QcVec3 QcDrifterBrain::shotAt(const QcArcherDefinition &archer, const QcVec3 &aim)
{
    if (archer.scatter <= 0.0) {
        return QcArcherBrain::spread(aim.normalized() * archer.speed, archer.spreadDegrees, m_random);
    }
    // Sideways only, more the farther it shoots, less as an angle (the original game's blaze).
    const double off = archer.scatter * std::sqrt(aim.length());
    return QcVec3{aim.x + normalDraw() * off, aim.y, aim.z + normalDraw() * off}.normalized() * archer.speed;
}

double QcDrifterBrain::normalDraw()
{
    // Box-Muller; 1 - u keeps the logarithm away from 0.
    const double radius = std::sqrt(-2.0 * std::log(1.0 - m_random.generateDouble()));
    return radius * std::cos(2.0 * std::numbers::pi * m_random.generateDouble());
}
