// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/bosses/QcHydraBrain.h"

#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/mobbrains/QcMobSteering.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// Ticks of `seconds`, at least one.
int ticksOf(double seconds)
{
    return std::max(1, static_cast<int>(std::lround(seconds / QcPhysics::tickDuration)));
}

QcVec3 middleOf(const QcHydraBrain::Foe &foe)
{
    return foe.feet + QcVec3{0.0, foe.height * 0.5, 0.0};
}

// `player` as a foe, if it can be one.
std::optional<QcHydraBrain::Foe> playerFoe(const QcMobManager::PlayerView &player)
{
    if (!player.alive || !player.targetable) {
        return std::nullopt;
    }
    return QcHydraBrain::Foe{player.entityId, player.position, QcPhysics::playerHeight};
}

// The creature `other` as a foe, if it can be one: awake and not undead.
std::optional<QcHydraBrain::Foe> creatureFoe(const QcCreature &other, const QcMobBrain::Context &context)
{
    const QcMobDefinition &kind = context.world.content().mobs()[other.type];
    if (other.dormant || kind.undead) {
        return std::nullopt;
    }
    return QcHydraBrain::Foe{other.id, other.motion.position, kind.height};
}

} // namespace

QcHydraBrain::QcHydraBrain(QRandomGenerator &random)
    : m_random(random)
{
}

QcPhysics::Steering QcHydraBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    // The content reader gives every creature of Ai::Hydra its data, the creature its state.
    if (!mob.hydra || !definition.hydra) {
        return {{}, 0.0};
    }
    QcHydraState &state = *mob.hydra;
    const int ticks = std::max(1, thinkTicks);
    state.ticks += ticks;
    state.feetY.reset();
    if (state.charging()) {
        charge(mob, definition, ticks);
        state.feetY = mob.motion.position.y;
        mob.lastSpeed = 0.0;
        return {{}, 0.0};
    }

    recover(mob, definition, context, ticks);
    // A confined one (a showcase's pen) neither chooses targets nor fires.
    const std::optional<Foe> target = mob.confined ? std::nullopt : mainTarget(mob, context, ticks);
    state.target = target ? target->id : 0;
    const bool chasesPlayer = target && std::ranges::any_of(*context.players, [&](const QcMobManager::PlayerView &player) {
        return player.entityId == target->id;
    });
    mob.chasing = chasesPlayer ? target->id : 0;
    if (!mob.confined) {
        for (int side = 0; side < 2; ++side) {
            if (state.ticks >= state.sideNext[static_cast<std::size_t>(side)]) {
                sideHead(mob, context, side);
            }
        }
    }
    if (!target) {
        return wander(mob, definition, ticks);
    }
    mainHead(mob, *definition.hydra, context, *target, ticks);
    return chase(mob, definition, *target);
}

void QcHydraBrain::charge(QcCreature &mob, const QcMobDefinition &definition, int ticks)
{
    QcHydraState &state = *mob.hydra;
    state.chargeTicks -= ticks;
    state.healTicks += ticks;
    const int healEvery = ticksOf(QcHydraDefinition::chargeHealSeconds);
    while (state.healTicks >= healEvery) {
        mob.health = std::min(definition.maxHealth, mob.health + definition.hydra->chargeHeal); // NOLINT(bugprone-unchecked-optional-access): think() checked it
        state.healTicks -= healEvery;
    }
    if (state.chargeTicks <= 0) {
        state.chargeTicks = 0;
        state.healTicks = 0;
        state.blastDue = true;
    }
}

void QcHydraBrain::recover(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int ticks)
{
    QcHydraState &state = *mob.hydra;
    state.regenerationTicks += ticks;
    while (state.regenerationTicks >= regenerationTicks) {
        const double seconds = regenerationTicks * QcPhysics::tickDuration;
        mob.health = std::min(definition.maxHealth, mob.health + definition.hydra->regeneration * seconds); // NOLINT(bugprone-unchecked-optional-access): think() checked it
        state.regenerationTicks -= regenerationTicks;
    }

    // The blocks around it break a moment after a fresh hurt (QcHydraState::noteHurt).
    if (state.breakTicks > 0) {
        state.breakTicks -= ticks;
        if (state.breakTicks <= 0) {
            state.breakTicks = 0;
            state.breakDue = context.griefing && !mob.confined;
        }
    }
}

std::optional<QcHydraBrain::Foe> QcHydraBrain::mainTarget(QcCreature &mob, const Context &context, int ticks)
{
    const QcHydraState &state = *mob.hydra;
    // It turns on whoever struck it (the role model's hurt-by goal).
    if (mob.struckBy != 0) {
        const std::optional<Foe> striker = foeOf(mob, mob.struckBy, context);
        mob.struckBy = 0;
        if (striker && (striker->feet - mob.motion.position).length() <= targetRange) {
            return striker;
        }
    }
    if (state.target != 0) {
        const std::optional<Foe> kept = foeOf(mob, state.target, context);
        if (kept && (kept->feet - mob.motion.position).length() <= targetRange) {
            return kept;
        }
    }

    // It looks for a new one in turns, staggered by its id.
    if ((context.tick + mob.id) % lookTicks >= static_cast<quint64>(ticks)) {
        return std::nullopt;
    }
    const QList<Foe> near = foesNear(mob, context, targetRange, targetHeight);
    const auto nearest = std::ranges::min_element(near, {}, [&](const Foe &foe) { return (foe.feet - mob.motion.position).length(); });
    return nearest != near.end() ? std::optional<Foe>(*nearest) : std::nullopt;
}

void QcHydraBrain::mainHead(QcCreature &mob, const QcHydraDefinition &hydra, const Context &context, const Foe &target, int ticks)
{
    QcHydraState &state = *mob.hydra;
    state.attackTicks -= ticks;
    if (state.attackTicks > 0) {
        return;
    }
    // Out of reach or out of sight when it is time, it waits a whole round again.
    state.attackTicks = ticksOf(hydra.attackSeconds);
    const QcVec3 aim = middleOf(target);
    const QcVec3 head = headOf(mob, 0);
    if ((aim - head).length() > hydra.attackRange || !QcLineOfSight::seesPoint(context.world, head, aim)) {
        return;
    }
    fire(mob, context, 0, aim, m_random.generateDouble() < hydra.chargedChance);
}

void QcHydraBrain::sideHead(QcCreature &mob, const Context &context, int slot)
{
    QcHydraState &state = *mob.hydra;
    const auto index = static_cast<std::size_t>(slot);
    state.sideNext[index] = state.ticks + sideMinTicks + m_random.bounded(sideSpreadTicks);

    // A head that found nothing for a while fires a charged skull at a random place nearby.
    if (state.sideIdle[index]++ > sideIdleShots) {
        const QcVec3 place = mob.motion.position + QcVec3{m_random.bounded(2.0 * idleReach) - idleReach,
                                                          m_random.bounded(2.0 * idleReachHeight) - idleReachHeight,
                                                          m_random.bounded(2.0 * idleReach) - idleReach};
        fire(mob, context, slot + 1, place, true);
        state.sideIdle[index] = 0;
    }

    quint32 &target = state.sideTargets[index];
    if (target != 0) {
        const std::optional<Foe> foe = foeOf(mob, target, context);
        const QcVec3 head = headOf(mob, slot + 1);
        if (foe && (foe->feet - mob.motion.position).length() <= sideRange && QcLineOfSight::seesPoint(context.world, head, middleOf(*foe))) {
            fire(mob, context, slot + 1, middleOf(*foe), false);
            state.sideNext[index] = state.ticks + sideShotTicks + m_random.bounded(sideShotSpreadTicks);
            state.sideIdle[index] = 0;
        } else {
            target = 0;
        }
        return;
    }
    const QList<Foe> near = foesNear(mob, context, sideSearch, sideSearchHeight);
    if (!near.isEmpty()) {
        target = near[static_cast<qsizetype>(m_random.bounded(static_cast<quint32>(near.size())))].id;
    }
}

void QcHydraBrain::fire(const QcCreature &mob, const Context &context, int head, const QcVec3 &point, bool charged)
{
    const QcHydraDefinition &hydra = *context.world.content().mobs()[mob.type].hydra; // NOLINT(bugprone-unchecked-optional-access): think() checked it
    const QcVec3 from = headOf(mob, head);
    const QcVec3 towards = point - from;
    if (!context.launches || towards.length() < 1e-6) {
        return;
    }
    const double speed = charged ? hydra.chargedSkullSpeed : hydra.skullSpeed;
    context.launches->append({mob.id, charged ? hydra.chargedSkull : hydra.skull, from, towards.normalized() * speed, QcProjectilePickup::Never, 1.0});
}

QcPhysics::Steering QcHydraBrain::chase(QcCreature &mob, const QcMobDefinition &definition, const Foe &target)
{
    // Above its target, in its armour level with it (the role model's flight).
    const QcHydraDefinition &hydra = *definition.hydra; // NOLINT(bugprone-unchecked-optional-access): think() checked it
    mob.hydra->feetY = target.feet.y + (hydra.armouredAt(mob.health, definition.maxHealth) ? 0.0 : hydra.hoverAbove);
    mob.yaw = QcMobSteering::yawTowards(target.feet - mob.motion.position);
    const QcVec3 flat{target.feet.x - mob.motion.position.x, 0.0, target.feet.z - mob.motion.position.z};
    if (flat.length() <= keepDistance) {
        mob.lastSpeed = 0.0;
        return {{}, 0.0};
    }
    mob.lastSpeed = definition.chaseSpeed;
    return {flat.normalized(), mob.lastSpeed};
}

QcPhysics::Steering QcHydraBrain::wander(QcCreature &mob, const QcMobDefinition &definition, int ticks)
{
    QcHydraState &state = *mob.hydra;
    state.headingTicks -= ticks;
    if (state.headingTicks <= 0 || mob.blockedLastTick) {
        const double angle = m_random.bounded(2.0 * std::numbers::pi);
        state.heading = {std::cos(angle), 0.0, std::sin(angle)};
        state.headingTicks = headingTicks + m_random.bounded(headingTicks);
    }
    mob.lastSpeed = definition.walkSpeed;
    return {state.heading, mob.lastSpeed};
}

QList<QcHydraBrain::Foe> QcHydraBrain::foesNear(const QcCreature &mob, const Context &context, double reach, double height) const
{
    // Only those near it in a box around it: a cheap test of the position first.
    const QcMobDefinition &definition = context.world.content().mobs()[mob.type];
    const QcVec3 &at = mob.motion.position;
    const auto within = [&](const QcVec3 &feet, double tall) {
        return std::abs(feet.x - at.x) <= reach && std::abs(feet.z - at.z) <= reach && feet.y + tall >= at.y - height
               && feet.y <= at.y + definition.height + height;
    };
    QList<Foe> foes;
    for (const QcMobManager::PlayerView &player : *context.players) {
        if (within(player.position, QcPhysics::playerHeight)) {
            if (const std::optional<Foe> foe = playerFoe(player)) {
                foes.append(*foe);
            }
        }
    }
    for (const QcCreature &other : context.mobs) {
        if (other.id != mob.id && within(other.motion.position, context.world.content().mobs()[other.type].height)) {
            if (const std::optional<Foe> foe = creatureFoe(other, context)) {
                foes.append(*foe);
            }
        }
    }
    return foes;
}

QcVec3 QcHydraBrain::headOf(const QcCreature &mob, int head)
{
    const double yaw = qDegreesToRadians(static_cast<double>(mob.yaw));
    const QcVec3 right{std::cos(yaw), 0.0, std::sin(yaw)};
    if (head <= 0) {
        return mob.motion.position + QcVec3{0.0, mainHeadHeight, 0.0};
    }
    const double side = head == 1 ? sideHeadSpacing : -sideHeadSpacing;
    return mob.motion.position + right * side + QcVec3{0.0, sideHeadHeight, 0.0};
}

std::optional<QcHydraBrain::Foe> QcHydraBrain::foeOf(const QcCreature &hydra, quint32 id, const Context &context)
{
    if (id == 0 || id == hydra.id) {
        return std::nullopt;
    }
    const auto player = std::ranges::find(*context.players, id, &QcMobManager::PlayerView::entityId);
    if (player != context.players->end()) {
        return playerFoe(*player);
    }
    const auto other = std::ranges::find(context.mobs, id, &QcCreature::id);
    return other != context.mobs.end() ? creatureFoe(*other, context) : std::nullopt;
}
