// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/animals/QcPetCare.h"

#include "common/content/registry/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/animals/QcPetOwners.h"
#include "server/animals/QcServerAnimals.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"

#include <algorithm>

QcPetCare::QcPetCare(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

bool QcPetCare::use(QcServerClient &client, quint32 mobId)
{
    QcCreature *mob = m_state.entities().creatures().find(mobId);
    if (!mob || mob->dormant || mob->type < 0 || mob->type >= m_state.content().mobs().size()) {
        return false;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    if (!definition.tameable) {
        return false;
    }

    // Out of reach or hidden: nothing happens, and nothing is eaten instead.
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0
        || !QcLineOfSight::canSee(m_state.world(), client, QcServerAnimals::shownBox(definition, *mob))) {
        return true;
    }

    const QcItemDefinition *held = m_state.heldItem(client);
    if (mob->isTame()) {
        return tend(client, *mob, definition, held);
    }
    if (!held || !definition.tameable->items.contains(held->name)) {
        return false;
    }
    // An angry one cannot be tamed and eats nothing (like the reference).
    if (mob->provokedBy != 0) {
        return true;
    }

    consume(client);
    if (m_state.random().generateDouble() < definition.tameable->chance) {
        tame(m_state.entities(), m_state.content(), *mob, QcPetOwners::keyOf(client));
        m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::EntityTamed, .entity = definition.name});
        m_sounds.play(m_state.content().mobs()[mob->type].ambientSound, centre);
    }
    return true;
}

bool QcPetCare::tend(QcServerClient &client, QcCreature &mob, const QcMobDefinition &definition, const QcItemDefinition *held)
{
    const QcTameDefinition &rules = *definition.tameable; // NOLINT(bugprone-unchecked-optional-access): use() checked it
    const QcVec3 centre = mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    // Anyone may feed a hurt pet.
    if (held && rules.healItems.contains(held->name) && mob.health < definition.maxHealth) {
        mob.health = std::min(definition.maxHealth, mob.health + rules.healAmount);
        noteUse(client, *held, definition);
        consume(client);
        m_sounds.play(QString::fromLatin1(QcSoundKeys::playerEat), centre, 0.6f);
        return true;
    }
    if (mob.tame->owner != QcPetOwners::keyOf(client)) {
        return false;
    }

    // A dye its collar takes, unless the collar has that colour already.
    const std::optional<quint32> color = held ? rules.collarColor(held->name) : std::nullopt;
    if (color && *color != mob.tame->collar) {
        mob.tame->collar = *color;
        noteUse(client, *held, definition);
        consume(client);
        m_state.entities().markStale(mob.id);
        return true;
    }
    if (color || (held && rules.healItems.contains(held->name))) {
        return true;
    }

    mob.tame->sitting = !mob.tame->sitting;
    mob.tame->target = 0;
    mob.pose = mob.tame->sitting ? QcPose::Sitting : QcPose::Standing;
    mob.path.clear();
    return true;
}

void QcPetCare::noteUse(QcServerClient &client, const QcItemDefinition &held, const QcMobDefinition &definition)
{
    m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::ItemUsedOnEntity, .item = held.name, .entity = definition.name});
}

void QcPetCare::tame(QcEntities &entities, const QcContentRegistry &content, QcCreature &mob, const QString &owner)
{
    const QcMobDefinition &wild = content.mobs()[mob.type];
    const int type = wild.tameable && wild.tameable->becomesType >= 0 ? wild.tameable->becomesType : mob.type;
    const QcMobDefinition &kind = content.mobs()[type];
    if (type != mob.type) {
        // A new kind keeps the share of health it had.
        mob.health = std::clamp(mob.health / wild.maxHealth * kind.maxHealth, 0.1, kind.maxHealth);
        mob.type = type;
    }

    QcTameState &tame = mob.tame.emplace();
    tame.owner = owner;
    tame.sitting = true;
    if (kind.tameable && kind.tameable->hasCollar()) {
        tame.collar = kind.tameable->collarColor(kind.tameable->collarDefault).value_or(0);
    }
    mob.pose = QcPose::Sitting;
    mob.provokedBy = 0;
    mob.angerTimer = 0.0;
    mob.struckBy = 0;
    mob.chasing = 0;
    // A pet follows its owner out of any enclosure.
    mob.confined = false;
    mob.path.clear();
    entities.creatures().keep(mob, true);
    // Its new kind and collar reach the clients as a new spawn.
    entities.markStale(mob.id);
}

void QcPetCare::consume(QcServerClient &client)
{
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
}
