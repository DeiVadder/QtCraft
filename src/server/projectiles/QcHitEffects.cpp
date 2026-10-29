// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/projectiles/QcHitEffects.h"

#include "common/content/items/QcProjectileDefinition.h"
#include "common/physics/QcPhysics.h"

#include <cmath>

QcHitEffects::QcHitEffects()
{
    // The damage effects together deal the speed damage (QcProjectileDefinition::damage is their sum).
    add(QString::fromLatin1(QcHitEffect::damage), [](const QcHitEffect &, double damage, Impact &impact) { impact.damage = damage; });
    add(QString::fromLatin1(QcHitEffect::knockback),
        [](const QcHitEffect &effect, double, Impact &impact) { impact.knockback = std::max(impact.knockback, effect.number); });
    add(QString::fromLatin1(QcHitEffect::ignite), [](const QcHitEffect &effect, double, Impact &impact) {
        impact.burnTicks = std::max(impact.burnTicks, static_cast<int>(std::lround(effect.number / QcPhysics::tickDuration)));
    });
    // Breaking is the projectile's own (QcProjectileDefinition::breaks); the body takes nothing.
    add(QString::fromLatin1(QcHitEffect::breaks), [](const QcHitEffect &, double, Impact &) {});
    // Teleporting the thrower happens where the projectile breaks (QcServerProjectiles, decision
    // E-460), on a body as on a block; the body takes nothing.
    add(QString::fromLatin1(QcHitEffect::teleport), [](const QcHitEffect &, double, Impact &) {});
    add(QString::fromLatin1(QcHitEffect::effect), [](const QcHitEffect &effect, double, Impact &impact) {
        impact.effects.append({effect.subject, effect.level, static_cast<int>(std::lround(effect.number / QcPhysics::tickDuration))});
    });
    // Spawning happens where the projectile breaks (QcServerProjectiles), at a body or a block.
    add(QString::fromLatin1(QcHitEffect::spawn), [](const QcHitEffect &, double, Impact &) {});
    // A splash reaches the bodies around the place it breaks (QcServerProjectiles, decision E-624).
    add(QString::fromLatin1(QcHitEffect::splash), [](const QcHitEffect &, double, Impact &) {});
}

QcHitEffects::Impact QcHitEffects::impactOf(const QcProjectileDefinition &definition, double damage) const
{
    Impact impact;
    for (const QcHitEffect &effect : definition.onHit) {
        if (const auto it = m_handlers.constFind(effect.type); it != m_handlers.constEnd()) {
            (*it)(effect, damage, impact);
        }
    }
    return impact;
}

int QcHitEffects::spawnCount(const QcHitEffect &effect, QRandomGenerator &random)
{
    if (random.generateDouble() >= effect.number) {
        return 0;
    }
    // The reference's egg: one chick in eight throws, four in a 32nd of those.
    return random.generateDouble() < effect.groupChance ? effect.groupCount : effect.count;
}

QcHitEffects::Impact QcHitEffects::splashImpact(const QcProjectileDefinition &definition, double share)
{
    Impact impact;
    for (const QcHitEffect &effect : definition.onHit) {
        if (effect.is(QcHitEffect::damage)) {
            impact.damage += effect.number * share;
        } else if (effect.is(QcHitEffect::effect)) {
            const int ticks = static_cast<int>(std::lround(effect.number * share / QcPhysics::tickDuration));
            if (ticks > 0) {
                impact.effects.append({effect.subject, effect.level, ticks});
            }
        }
    }
    return impact;
}
