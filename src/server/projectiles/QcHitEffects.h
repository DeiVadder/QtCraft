#pragma once

#include "common/content/definitions/QcHitEffect.h"

#include <QHash>
#include <QString>

#include <functional>

struct QcProjectileDefinition;

// What the effects of a projectile's hit on a body add up to (decision E-427). Each type of
// QcHitEffect::types has a handler here that adds to the Impact; QcServerProjectiles then deals the
// impact at once: the damage with the knockback as one hit (armour, the short invulnerability,
// sounds, loot), then the burning. A new type is a row of QcHitEffect::types (so the content keeps
// it) and a handler here (add); a type without a handler does nothing.
class QcHitEffects
{
public:
    // What one hit does to the body.
    struct Impact
    {
        double damage = 0.0;
        // Strength of the push along the flight; 1 is a blow, 0 none.
        double knockback = 0.0;
        // Ticks the body burns from now on at least.
        int burnTicks = 0;
    };

    // `damage`: the hit's damage from the projectile's speed (QcProjectileFlight::damageAt), which
    // the damage effects deal.
    using Handler = std::function<void(const QcHitEffect &effect, double damage, Impact &impact)>;

    // The handlers of the base types.
    QcHitEffects();

    void add(const QString &type, const Handler &handler) { m_handlers.insert(type, handler); }
    // The impact of a hit of `definition` with the speed damage `damage`: the handlers aggregate
    // (damage once, the strongest knockback, the longest burning), so the order does not matter.
    Impact impactOf(const QcProjectileDefinition &definition, double damage) const;

private:
    QHash<QString, Handler> m_handlers;
};
