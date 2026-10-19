#pragma once

#include "common/content/definitions/QcHitEffect.h"
#include "common/gameplay/QcStatusEffects.h"

#include <QHash>
#include <QRandomGenerator>
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
        // Status effects the body gets when the hit counts (decision E-450).
        QList<QcStatusEffect> effects = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
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
    // How many creatures a spawn effect brings where its projectile breaks (decision E-450): with the
    // effect's chance its count, of those with groupChance its groupCount, else none. Two draws of
    // `random` when the first succeeds, one otherwise.
    static int spawnCount(const QcHitEffect &effect, QRandomGenerator &random);
    // What a splash of `definition` (decision E-624) does to a body `share` of the full strength
    // (1 at the place it broke, 0 at the splash's radius): the amounts of its damage effects and
    // its status effects for that share of their ticks, scaled alike.
    static Impact splashImpact(const QcProjectileDefinition &definition, double share);

private:
    QHash<QString, Handler> m_handlers;
};
