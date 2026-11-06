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

const QcMobManager::PlayerView *riderOf(const QcCreature &mob, const QList<QcMobManager::PlayerView> &players)
{
    const auto it = std::ranges::find(players, mob.rider, &QcMobManager::PlayerView::entityId);
    return mob.rider != 0 && it != players.end() ? &*it : nullptr;
}

QcVec3 middleOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
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
            charge(mob, definition, *target, context, seconds);
        } else {
            mob.drifter->chargeSeconds = 0.0;
        }
    }
    return drift(mob, definition, context, seconds);
}

QcPhysics::Steering QcDrifterBrain::drift(QcCreature &mob, const QcMobDefinition &definition, const Context &context, double seconds)
{
    QcDrifterState &state = *mob.drifter;
    state.headingSeconds -= seconds;
    if (state.headingSeconds <= 0.0 || mob.blockedLastTick) {
        state.heading = newHeading(mob, definition, context);
        state.headingSeconds = definition.drift.seconds * (0.5 + m_random.generateDouble());
    }

    // One that keeps to lava waits at the shore for a heading that leads back over it.
    const bool stops = definition.drift.keepsToLava && !lavaAhead(mob, state.heading, 1.0, context.world)
                       && lavaAhead(mob, {}, 0.0, context.world);
    mob.lastSpeed = stops ? 0.0 : definition.walkSpeed;
    return {stops ? QcVec3{} : state.heading, mob.lastSpeed};
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
        state.chargeSeconds = 0.0;
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
    if (state.chargeSeconds < archer.drawSeconds || !context.launches) {
        return;
    }

    state.chargeSeconds = 0.0;
    if (!QcLineOfSight::seesPoint(context.world, middle, eyes)) {
        return;
    }
    const QcVec3 towards = (eyes - middle).normalized();
    const QcVec3 from = middle + towards * (definition.width * 0.5 + 0.8);
    context.launches->append({mob.id, archer.projectile, from, QcArcherBrain::spread(towards * archer.speed, archer.spreadDegrees, m_random),
                              QcProjectilePickup::Never, 1.0});
    mob.attackCooldown = archer.cooldownSeconds;
}
