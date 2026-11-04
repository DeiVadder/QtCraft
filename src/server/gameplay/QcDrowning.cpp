// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcDrowning.h"

#include "common/gameplay/QcAirSupply.h"
#include "common/gameplay/QcEffectRules.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

QcDrowning::QcDrowning(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
{
}

void QcDrowning::tick()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            tickPlayer(*client);
        }
    }
    tickCreatures();
}

bool QcDrowning::differs(QcMsg::PlayerAir sent, QcMsg::PlayerAir now)
{
    return sent.submerged != now.submerged || sent.maxAir != now.maxAir || QcAirSupply::bubblesOf(sent.air, sent.maxAir) != QcAirSupply::bubblesOf(now.air, now.maxAir)
           || (sent.air == sent.maxAir) != (now.air == now.maxAir);
}

void QcDrowning::tickPlayer(QcServerClient &client)
{
    // A spectator, a creative player, the dead and water breathing breathe freely: the air fills up.
    const bool breathes = !client.hasBody() || client.rules().invulnerable || QcEffectRules::breathesUnderWater(m_state.content(), client.effects.all());
    const QcVec3 eyes = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const bool submerged = !breathes && QcAirSupply::submergedAt(m_state.world(), eyes);

    // Drowning respects the invulnerability after a hurt like starving (decision E-429); its
    // rhythm of a second is longer than the default window.
    if (client.air.tick(submerged) && m_state.info().drowningDamage) {
        m_health.damage(client, QcAirSupply::drowningDamage, {}, QcPlayerHealth::DamageKind::Drowning);
    }
    sendAir(client, submerged);
}

void QcDrowning::sendAir(QcServerClient &client, bool submerged)
{
    const QcMsg::PlayerAir now{client.air.air(), QcAirSupply::maxAir, submerged};
    if (client.sentAir && !differs(*client.sentAir, now)) {
        return;
    }
    client.sentAir = now;
    client.connection->send(now);
}

void QcDrowning::tickCreatures()
{
    const bool damages = m_state.info().drowningDamage;
    QList<quint32> drowning;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        const bool submerged = !mob.dormant && !definition.breathesUnderWater
                               && QcAirSupply::submergedAt(m_state.world(), QcEnvironmentDamage::eyesOf(mob, definition));
        if (mob.air.tick(submerged) && damages) {
            drowning.append(mob.id);
        }
    }

    // Hurting may remove a creature, so the damage is dealt after the walk over them. A drowned
    // creature leaves its loot, as one that burned or dried out.
    for (const quint32 id : std::as_const(drowning)) {
        QcServerCombat::woundCreature(m_state, m_sounds, id, QcAirSupply::drowningDamage);
    }
}
