// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcBodyBurning.h"

#include "common/gameplay/QcEffectRules.h"
#include "common/light/QcLightTable.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

#include <algorithm>

QcBodyBurning::QcBodyBurning(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
{
}

void QcBodyBurning::tick()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            tickPlayer(*client);
        }
    }
    tickCreatures();
    tickWetCreatures();
    tickDryCreatures();
}

bool QcBodyBurning::inWater(const QcVec3 &feet, double height) const
{
    const QcContentRegistry &content = m_state.content();
    for (const double up : {0.1, height * 0.5}) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(QcPhysics::blockContaining(feet + QcVec3{0.0, up, 0.0}));
        if (block && content.isValidBlock(*block) && content.block(*block).isWater()) {
            return true;
        }
    }
    return false;
}

bool QcBodyBurning::rainsOn(const QcVec3 &feet) const
{
    // The cell above the feet: a body on farmland, a slab, a carpet or stairs has its feet in the
    // cell of that block, which is the top of the column, so rain never falls on that cell, but it
    // reaches the body when it falls on the cell above. Rain on the cell above means nothing covers
    // the body from above, whatever its height (decision E-397).
    return m_rainsOn && m_rainsOn(QcPhysics::blockContaining(feet).offset(0, 1, 0));
}

int QcBodyBurning::burnTicksAfter(const QcEntity &body, double width, double height, int alightTicks) const
{
    const QcVec3 &feet = body.motion.position;
    const QcContactDamage::Touch touch = m_state.contactDamage().touching(m_state.world(), QcPhysics::boxAt(feet, width, height));
    const int alight = std::max(touch.alightTicks, alightTicks);
    if (body.burnTicks <= 0 && alight <= 0) {
        return 0;
    }
    if (inWater(feet, height) || rainsOn(feet)) {
        return 0;
    }
    return std::max(body.burnTicks - 1, alight);
}

bool QcBodyBurning::inSunlight(const QcEntity &body, double height) const
{
    // By day, with nothing above the head that keeps out the sky (shade, a roof, leaves).
    if (QcDayCycle::isNight(m_state.info().worldTime)) {
        return false;
    }
    const QcBlockPos head = QcPhysics::blockContaining(body.motion.position + QcVec3{0.0, height * 0.9, 0.0});
    return m_state.world().skyLight(head) >= QcLightTable::maxLevel;
}

bool QcBodyBurning::damageDue(const QcEntity &body) const
{
    return (m_state.clock().tick() + body.id) % static_cast<quint64>(burnIntervalTicks) == 0;
}

void QcBodyBurning::tickPlayer(QcServerClient &client)
{
    const bool wasBurning = client.burnTicks > 0;
    if (client.dead || client.rules().invulnerable) {
        client.burnTicks = 0;
    } else {
        client.burnTicks = burnTicksAfter(client, QcPhysics::playerWidth, QcPhysics::playerHeight);
        if (client.burnTicks > 0 && damageDue(client) && !QcEffectRules::fireProof(m_state.content(), client.effects.all())) {
            m_health.damage(client, burnDamage, {}, QcPlayerHealth::DamageKind::Armoured);
        }
    }

    // Dying in the flames put them out; the death sent the health already.
    if (client.dead) {
        client.burnTicks = 0;
    }
    if (wasBurning != (client.burnTicks > 0)) {
        m_health.sendHealth(client);
    }
}

void QcBodyBurning::tickCreatures()
{
    // Hurting may remove a creature, so the burning ones are collected first.
    QList<quint32> hurt;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        if (mob.dormant) {
            continue;
        }
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        const bool sunburn = definition.burnsInDaylight && inSunlight(mob, definition.height);
        mob.burnTicks = burnTicksAfter(mob, definition.width, definition.height, sunburn ? sunlightTicks : 0);
        const bool fireProof = mob.effects && QcEffectRules::fireProof(m_state.content(), mob.effects->all());
        if (mob.burnTicks > 0 && damageDue(mob) && !fireProof) {
            hurt.append(mob.id);
        }
    }

    for (const quint32 id : std::as_const(hurt)) {
        QcServerCombat::woundCreature(m_state, m_sounds, id, burnDamage);
    }
}

void QcBodyBurning::tickWetCreatures()
{
    // Hurting may remove a creature, so the wet ones are collected first.
    QList<quint32> wet;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (definition.waterDamage > 0.0 && !mob.dormant && (m_state.clock().tick() + mob.id) % static_cast<quint64>(wetIntervalTicks) == 0
            && (inWater(mob.motion.position, definition.height) || rainsOn(mob.motion.position))) {
            wet.append(mob.id);
        }
    }

    QcMobManager &creatures = m_state.entities().creatures();
    for (const quint32 id : std::as_const(wet)) {
        const QcCreature *mob = creatures.find(id);
        if (!mob) {
            continue;
        }
        const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
        QcServerCombat::woundCreature(m_state, m_sounds, id, definition.waterDamage);
        if (definition.teleport && definition.teleport->whenWet) {
            creatures.teleport(id, m_state.world());
        }
    }
}

void QcBodyBurning::tickDryCreatures()
{
    QList<std::pair<quint32, double>> dry;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const std::optional<QcAquaticDefinition> &aquatic = m_state.content().mobs()[mob.type].aquatic;
        if (!aquatic || mob.dormant) {
            continue;
        }
        const int air = static_cast<int>(std::lround(aquatic->airSeconds * QcPhysics::tickRate));
        if (mob.dryTicks > air && (mob.dryTicks - air) % static_cast<int>(QcPhysics::tickRate) == 0) {
            dry.append({mob.id, aquatic->suffocationDamage});
        }
    }
    for (const auto &[id, damage] : std::as_const(dry)) {
        QcServerCombat::woundCreature(m_state, m_sounds, id, damage);
    }
}
