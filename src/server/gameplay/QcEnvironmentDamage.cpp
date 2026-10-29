// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcEnvironmentDamage.h"

#include "common/physics/QcPhysics.h"
#include "common/physics/QcPose.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"

#include <algorithm>

QcEnvironmentDamage::QcEnvironmentDamage(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
{
}

namespace {

// Damage of the solid block under the centre of the feet (magma, decision E-209); a liquid next to
// the block a body stands on does not count.
double damageFromBelow(const QcServerState &state, const QcVec3 &feet)
{
    const std::optional<QcBlockId> below = state.world().blockAt(QcPhysics::blockContaining(feet - QcVec3{0.0, 0.01, 0.0}));
    if (!below || !state.content().isValidBlock(*below) || !state.content().block(*below).solid) {
        return 0.0;
    }
    return state.content().block(*below).contactDamage;
}

// Damage from what a body is inside of or stands on.
double contactDamageOf(const QcServerState &state, const QcVec3 &feet, double width, double height)
{
    return std::max(state.contactDamage().in(state.world(), QcPhysics::boxAt(feet, width, height)), damageFromBelow(state, feet));
}

} // namespace

void QcEnvironmentDamage::applyContactDamage()
{
    if (!m_state.contactDamage().isPossibleIn(m_state.world())) {
        return;
    }

    for (const auto &client : m_state.clients()) {
        if (!client->hasBody() || !contactDue(*client)) {
            continue;
        }

        const double damage = contactDamageOf(m_state, client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight);
        if (damage > 0.0) {
            m_health.damage(*client, damage, {}, QcPlayerHealth::DamageKind::Armoured);
        }
    }

    // Hurting may remove a mob, so the damage is collected before any is dealt.
    QList<std::pair<quint32, double>> touched;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        if (!contactDue(mob)) {
            continue;
        }

        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (const double damage = contactDamageOf(m_state, mob.motion.position, definition.width, definition.height);
            damage > 0.0) {
            touched.append({mob.id, damage});
        }
    }

    for (const auto &[id, damage] : std::as_const(touched)) {
        const QcCreature *mob = m_state.entities().creatures().find(id);
        if (!mob) {
            continue;
        }

        const QString sound = m_state.content().mobs()[mob->type].deathSound;
        const QcVec3 position = mob->motion.position;

        if (m_state.entities().creatures().hurt(id, damage)) {
            m_sounds.play(sound, position);
            // Burnt creatures leave no loot.
            m_state.entities().noteDeath(id);
        }
    }

    m_state.entities().items().removeWhere([this](const QcItemEntity &item) {
        return m_state.contactDamage().touching(m_state.world(), QcPhysics::boxAt(item.motion.position, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize)).burnsItems;
    });
}

bool QcEnvironmentDamage::contactDue(const QcEntity &body) const
{
    return (m_state.clock().tick() + body.id) % static_cast<quint64>(contactIntervalTicks) == 0;
}

namespace {

bool eyesInsideBlock(const QcServerState &state, const QcVec3 &eyes)
{
    const std::optional<QcBlockId> block = state.world().blockAt(QcPhysics::blockContaining(eyes));
    return block && state.content().isValidBlock(*block) && state.content().block(*block).solid && state.content().state(*block).fullCube
           && state.content().state(*block).opaque;
}

} // namespace

QcVec3 QcEnvironmentDamage::eyesOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return QcPoses::eyesAt(mob.pose, mob.motion.position, mob.yaw, definition.width, definition.height);
}

void QcEnvironmentDamage::applySuffocation()
{
    if (m_state.clock().tick() % suffocationIntervalTicks != 0) {
        return;
    }

    for (const auto &client : m_state.clients()) {
        const QcVec3 eyes = client->motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        if (client->hasBody() && eyesInsideBlock(m_state, eyes)) {
            m_health.damage(*client, suffocationDamage, {}, QcPlayerHealth::DamageKind::Unblockable);
        }
    }

    QList<quint32> suffocating;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (!mob.dormant && eyesInsideBlock(m_state, eyesOf(mob, definition))) {
            suffocating.append(mob.id);
        }
    }
    for (const quint32 id : std::as_const(suffocating)) {
        if (m_state.entities().creatures().hurt(id, suffocationDamage)) {
            m_state.entities().noteDeath(id);
        }
    }
}
