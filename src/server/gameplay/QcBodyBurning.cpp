#include "server/gameplay/QcBodyBurning.h"

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
}

bool QcBodyBurning::inWater(const QcVec3 &feet, double height) const
{
    const QcContentRegistry &content = m_state.content();
    for (const double up : {0.1, height * 0.5}) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(QcPhysics::blockContaining(feet + QcVec3{0.0, up, 0.0}));
        if (block && content.isValidBlock(*block) && content.block(*block).liquid && content.block(*block).fire.setsAlightSeconds == 0) {
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
    return (m_state.clock().tick() + body.id) % quint64(burnIntervalTicks) == 0;
}

void QcBodyBurning::tickPlayer(QcServerClient &client)
{
    const bool wasBurning = client.burnTicks > 0;
    if (client.dead || client.rules().invulnerable) {
        client.burnTicks = 0;
    } else {
        client.burnTicks = burnTicksAfter(client, QcPhysics::playerWidth, QcPhysics::playerHeight);
        if (client.burnTicks > 0 && damageDue(client)) {
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
        if (mob.burnTicks > 0 && damageDue(mob)) {
            hurt.append(mob.id);
        }
    }

    for (const quint32 id : std::as_const(hurt)) {
        const QcCreature *mob = m_state.entities().creatures().find(id);
        if (!mob) {
            continue;
        }
        // Like a hit: the hurt sound when the damage takes as a fresh hit (not while the creature is
        // still invulnerable from the last, decision E-429), the death sound and the loot when it dies.
        const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
        const QList<QcItemDrop> drops = mob->isYoung() ? QList<QcItemDrop>() : definition.drops;
        const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
        const bool fresh = mob->hurt.wouldTake(burnDamage) == QcHurtWindow::Take::Fresh;
        if (m_state.entities().creatures().hurt(id, burnDamage)) {
            m_sounds.play(definition.deathSound, centre);
            m_state.entities().noteDeath(id);
            QcServerCombat::dropLoot(m_state, drops, centre);
        } else if (fresh) {
            m_sounds.play(definition.hurtSound, centre);
        }
    }
}
