// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcMobDrinking.h"

#include "common/gameplay/QcAirSupply.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/gameplay/QcServerEffects.h"

#include <algorithm>
#include <cmath>

namespace {

quint64 ticksOf(double seconds)
{
    return static_cast<quint64>(std::max<qint64>(1, std::llround(seconds / QcPhysics::tickDuration)));
}

} // namespace

QcMobDrinking::QcMobDrinking(QcServerState &state, QcServerSounds &sounds)
    : m_state(state)
    , m_sounds(sounds)
{
}

void QcMobDrinking::tick()
{
    const quint64 now = m_state.clock().tick();
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (!definition.drinks || !mob.archer.has() || mob.dormant || mob.noAi || mob.health <= 0.0) {
            continue;
        }

        QcArcherState &state = *mob.archer;
        if (state.drinking()) {
            if (now - static_cast<quint64>(state.drinkStart) >= ticksOf(definition.drinks->seconds)) {
                finish(mob);
            }
            continue;
        }
        // Begins with the drink's sound, like the reference; the draw ends.
        if (const QcMobDrinkDefinition::Potion *potion = choose(mob, definition)) {
            state.drinkStart = static_cast<qint64>(now);
            state.drinkItem = potion->itemId;
            state.drawStart = -1;
            m_state.entities().markStale(mob.id);
            if (!definition.drinks->sound.isEmpty()) {
                m_sounds.play(definition.drinks->sound, mob.motion.position + QcVec3{0.0, definition.height * 0.8, 0.0});
            }
        }
    }
}

const QcMobDrinkDefinition::Potion *QcMobDrinking::choose(const QcCreature &mob, const QcMobDefinition &definition)
{
    if (!definition.drinks) {
        return nullptr;
    }

    // Every potion draws its chance whether or not its need holds, like the reference's checks.
    for (const QcMobDrinkDefinition::Potion &potion : definition.drinks->potions) {
        const bool lucky = m_state.random().generateDouble() < potion.chance;
        if (lucky && needs(mob, definition, potion) && !hasEffectOf(mob, definition, potion.itemId)) {
            return &potion;
        }
    }
    return nullptr;
}

bool QcMobDrinking::needs(const QcCreature &mob, const QcMobDefinition &definition, const QcMobDrinkDefinition::Potion &potion) const
{
    switch (potion.need) {
    case QcMobDrinkDefinition::Need::Underwater:
        return QcAirSupply::submergedAt(m_state.world(), QcEnvironmentDamage::eyesOf(mob, definition));
    case QcMobDrinkDefinition::Need::Burning:
        return mob.burnTicks > 0;
    case QcMobDrinkDefinition::Need::Hurt:
        return mob.health < definition.maxHealth;
    case QcMobDrinkDefinition::Need::TargetFar:
        return mob.archer->target != 0 && (mob.archer->targetPosition - mob.motion.position).length() > potion.distance;
    }
    return false;
}

bool QcMobDrinking::hasEffectOf(const QcCreature &mob, const QcMobDefinition &definition, QcItemId item) const
{
    const QcItemDefinition &potion = m_state.content().item(item);
    if (!potion.drink) {
        return false;
    }
    // Instant effects last no time: a potion of healing is drunk whenever the need holds. An
    // effect it is immune to never takes, so it would drink that potion again and again.
    return std::ranges::any_of(potion.drink->effects, [&mob, &definition](const QcPotionEffect &effect) {
        const bool has = mob.effects.has() && mob.effects->find(effect.effect) != nullptr;
        return effect.seconds > 0.0 && (has || definition.immuneTo.contains(effect.effect));
    });
}

QList<QcItemDrop> QcMobDrinking::heldDrops(const QcContentRegistry &content, const QcCreature &mob)
{
    const QcMobDefinition &definition = content.mobs()[mob.type];
    if (!definition.drinks || !mob.archer.has() || !mob.archer->drinking() || mob.archer->drinkItem == qcNoItem) {
        return {};
    }
    return {QcItemDrop{.item = content.item(mob.archer->drinkItem).name, .count = 1, .chance = definition.drinks->dropChance}};
}

void QcMobDrinking::finish(QcCreature &mob)
{
    QcArcherState &state = *mob.archer;
    const QcItemDefinition &potion = m_state.content().item(state.drinkItem);
    if (potion.drink) {
        for (const QcPotionEffect &effect : potion.drink->effects) {
            const int ticks = static_cast<int>(std::lround(effect.seconds / QcPhysics::tickDuration));
            QcServerEffects::giveCreature(m_state.content(), m_state.entities().creatures(), mob.id, {effect.effect, effect.level, ticks});
        }
    }
    state.drinkStart = -1;
    state.drinkItem = qcNoItem;
    m_state.entities().markStale(mob.id);
    ++m_drinks;
}
