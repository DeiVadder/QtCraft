#include "server/projectiles/QcHitEffects.h"

#include "common/content/definitions/QcProjectileDefinition.h"
#include "common/physics/QcPhysics.h"

#include <cmath>

QcHitEffects::QcHitEffects()
{
    // The damage effects together deal the speed damage (QcProjectileDefinition::damage is their sum).
    add(QString::fromLatin1(QcHitEffect::damage), [](const QcHitEffect &, double damage, Impact &impact) { impact.damage = damage; });
    add(QString::fromLatin1(QcHitEffect::knockback),
        [](const QcHitEffect &effect, double, Impact &impact) { impact.knockback = std::max(impact.knockback, effect.number); });
    add(QString::fromLatin1(QcHitEffect::ignite), [](const QcHitEffect &effect, double, Impact &impact) {
        impact.burnTicks = std::max(impact.burnTicks, int(std::lround(effect.number / QcPhysics::tickDuration)));
    });
    // Breaking is the projectile's own (QcProjectileDefinition::breaks); the body takes nothing.
    add(QString::fromLatin1(QcHitEffect::breaks), [](const QcHitEffect &, double, Impact &) {});
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
