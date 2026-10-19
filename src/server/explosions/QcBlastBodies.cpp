#include "server/explosions/QcBlastBodies.h"

#include "server/core/QcServerState.h"
#include "server/explosions/QcBlastField.h"
#include "server/explosions/QcExplosionRays.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

#include <QList>

#include <algorithm>

namespace {

// The middle of a box: distances and pushes are taken from there.
QcVec3 middleOf(const QcAabb &box)
{
    return (box.min + box.max) * 0.5;
}

} // namespace

QcBlastBodies::QcBlastBodies(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
{
}

double QcBlastBodies::impact(double distance, double power, double exposure)
{
    const double reach = QcExplosionRays::reachFactor * power;
    if (reach <= 0.0 || distance >= reach) {
        return 0.0;
    }
    return (1.0 - distance / reach) * std::clamp(exposure, 0.0, 1.0);
}

double QcBlastBodies::damageOf(double impact, double power)
{
    return (impact * impact + impact) / 2.0 * 7.0 * QcExplosionRays::reachFactor * power + 1.0;
}

QcBlastBodies::Hits QcBlastBodies::hurt(QcBlastField &field, const QcVec3 &centre, double power)
{
    Hits hits;
    hits.players = hurtPlayers(field, centre, power);
    hits.creatures = hurtCreatures(field, centre, power);
    return hits;
}

int QcBlastBodies::hurtPlayers(QcBlastField &field, const QcVec3 &centre, double power)
{
    int hit = 0;
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || client->dead || client->rules().invulnerable) {
            continue;
        }
        const std::optional<QcAabb> box = m_state.entities().boxOf(*client);
        if (!box) {
            continue;
        }

        const QcVec3 away = middleOf(*box) - centre;
        const double distance = away.length();
        if (distance >= QcExplosionRays::reachFactor * power) {
            continue;
        }
        const double strength = impact(distance, power, QcExplosionRays::exposure(field, centre, *box));
        if (strength <= 0.0) {
            continue;
        }

        ++hit;
        m_health.damage(*client, damageOf(strength, power), away, QcPlayerHealth::DamageKind::Armoured);
        if (!client->dead) {
            QcPlayerHealth::knock(*client, away, strength * blastKnockback);
        }
        m_health.sendHealth(*client);
    }
    return hit;
}

int QcBlastBodies::hurtCreatures(QcBlastField &field, const QcVec3 &centre, double power)
{
    // Hurting may remove a creature, so the blows are worked out first.
    QList<QcServerCombat::Blow> blows;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        if (mob.dormant || (mob.motion.position - centre).length() >= QcExplosionRays::reachFactor * power + 2.0) {
            continue;
        }
        const std::optional<QcAabb> box = m_state.entities().boxOf(mob);
        if (!box) {
            continue;
        }
        const double distance = (middleOf(*box) - centre).length();
        const double strength = impact(distance, power, QcExplosionRays::exposure(field, centre, *box));
        if (strength > 0.0) {
            blows.append({.creature = mob.id, .damage = damageOf(strength, power), .from = centre, .knockback = strength * blastKnockback});
        }
    }

    for (const QcServerCombat::Blow &blow : std::as_const(blows)) {
        QcServerCombat::hurtCreature(m_state, m_sounds, blow);
    }
    return int(blows.size());
}
