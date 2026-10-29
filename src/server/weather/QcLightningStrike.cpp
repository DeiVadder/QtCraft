// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/weather/QcLightningStrike.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcFireIgnition.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

#include <algorithm>
#include <cmath>

QcLightningStrike::QcLightningStrike(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcFireIgnition &ignition)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
    , m_ignition(ignition)
{
}

void QcLightningStrike::strike(QcBlockPos pos)
{
    ++m_numbers.strikes;
    hitPlayers(pos);
    hitCreatures(pos);
    m_numbers.fires += static_cast<quint64>(m_ignition.lightning(pos, m_state.content().weather().lightning.sparks));
}

bool QcLightningStrike::withinReach(QcBlockPos pos, const QcVec3 &feet) const
{
    // A box around the struck cell like the reference game: `radius` to each side and below,
    // reachUp above.
    const double radius = m_state.content().weather().lightning.radius;
    const QcVec3 centre{pos.x + 0.5, static_cast<double>(pos.y), pos.z + 0.5};
    return std::abs(feet.x - centre.x) <= radius + 0.5 && std::abs(feet.z - centre.z) <= radius + 0.5 && feet.y >= centre.y - radius
           && feet.y <= centre.y + reachUp;
}

void QcLightningStrike::hitPlayers(QcBlockPos pos)
{
    const QcWeatherDefinition::Lightning &lightning = m_state.content().weather().lightning;
    for (const auto &client : m_state.clients()) {
        if (!client->hasBody() || client->rules().invulnerable
            || !withinReach(pos, client->motion.position)) {
            continue;
        }

        ++m_numbers.playersHit;
        // Alight first, so the health sent with the damage says it burns.
        client->burnTicks = std::max(client->burnTicks, lightning.burnSeconds * static_cast<int>(QcPhysics::tickRate));
        m_health.damage(*client, lightning.damage, client->motion.position - QcVec3{pos.x + 0.5, static_cast<double>(pos.y), pos.z + 0.5},
                        QcPlayerHealth::DamageKind::Armoured);
        if (client->dead) {
            client->burnTicks = 0;
        }
        m_health.sendHealth(*client);
    }
}

void QcLightningStrike::hitCreatures(QcBlockPos pos)
{
    const QcWeatherDefinition::Lightning &lightning = m_state.content().weather().lightning;
    const QcVec3 from{pos.x + 0.5, static_cast<double>(pos.y), pos.z + 0.5};

    // Hurting may remove a creature, so the struck ones are collected first.
    QList<quint32> struck;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        if (!mob.dormant && withinReach(pos, mob.motion.position)) {
            mob.burnTicks = std::max(mob.burnTicks, lightning.burnSeconds * static_cast<int>(QcPhysics::tickRate));
            struck.append(mob.id);
        }
    }

    for (const quint32 id : std::as_const(struck)) {
        const QcCreature *mob = m_state.entities().creatures().find(id);
        if (!mob) {
            continue;
        }

        ++m_numbers.creaturesHit;
        // Like a hit: the hurt sound when the damage takes, the death sound and the loot when it dies.
        const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
        const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
        const bool fresh = mob->hurt.wouldTake(lightning.damage) == QcHurtWindow::Take::Fresh;
        if (const std::optional<QList<QcItemDrop>> drops = m_state.entities().creatures().damage({.creature = id, .damage = lightning.damage, .from = from})) {
            m_sounds.play(definition.deathSound, centre);
            m_state.entities().noteDeath(id);
            QcServerCombat::dropLoot(m_state, *drops, centre);
        } else if (fresh) {
            m_sounds.play(definition.hurtSound, centre);
        }
    }
}
