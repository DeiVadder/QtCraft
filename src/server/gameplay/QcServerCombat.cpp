// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcServerCombat.h"

#include "common/gameplay/QcEnchanting.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcToolWear.h"
#include "server/golems/QcGolemGrudges.h"
#include "server/items/QcServerExperience.h"
#include "server/mobs/QcPetOwners.h"

QcServerCombat::QcServerCombat(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

void QcServerCombat::attack(QcServerClient &client, quint32 entityId)
{
    if (client.dead || client.attackCooldown > 0.0) {
        return;
    }

    // The pets of other players are spared (decision E-480).
    const QcCreature *mob = m_state.entities().creatures().find(entityId);
    if (!mob || QcPetOwners::isOthersPet(m_state, *mob, client.id)) {
        return;
    }

    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};

    // Generous slack for the mob's size and movement since the client aimed; no hits through walls.
    if ((centre - eye).length() > attackReach + definition.width + 1.0
        || !QcLineOfSight::canSee(m_state.world(), client, QcPhysics::boxAt(mob->motion.position, definition.width, definition.height))) {
        return;
    }

    client.attackCooldown = playerAttackCooldown;
    client.swung = true;
    // The player's pets attack it too (decision E-480).
    client.foes.struck = entityId;
    client.foes.struckTick = m_state.clock().tick();
    client.hunger.exhaust(QcHunger::attack);

    const QcItemDefinition *held = m_state.heldItem(client);
    // Sharpness adds to the weapon's damage (decision E-864).
    const int sharpness = QcEnchanting::levelOf(m_state.content(), client.inventory.selectedStack(), QcEnchantmentEffect::Sharpness);
    const double damage = (held ? held->attackDamage : 1.0) + QcEnchanting::damageBonus(sharpness);
    hurtCreature(m_state, m_sounds, {.creature = entityId, .damage = damage, .from = client.motion.position, .attacker = client.id});

    // Weapons wear like tools, except in creative mode (decision E-282).
    if (held && held->durability > 0 && !client.rules().infiniteItems) {
        const int slot = client.inventory.selectedHotbarSlot();
        client.inventory.setSlot(slot, QcToolWear::worn(m_state, client.inventory.slot(slot)));
        m_inventory.send(client);
    }
}

bool QcServerCombat::hurtCreature(QcServerState &state, QcServerSounds &sounds, const Blow &blow)
{
    QcCreature *mob = state.entities().creatures().find(blow.creature);
    if (!mob || QcPetOwners::isOthersPet(state, *mob, blow.attacker)) {
        return false;
    }

    const QcMobDefinition &definition = state.content().mobs()[mob->type];
    const QcVec3 lootPosition = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    // Young animals give neither loot nor experience (like the reference); read before the hit
    // removes a dead creature.
    const bool young = mob->isYoung();
    const QString named = mob->name;

    // Hits during the short invulnerability after a hit do nothing and make no sound, unless they
    // are stronger: those deal the difference, silently (decision E-429).
    const QcHurtWindow::Take take = mob->hurt.wouldTake(blow.damage);
    if (take != QcHurtWindow::Take::Ignored) {
        const auto attacker = std::ranges::find_if(state.clients(), [&](const auto &client) { return client->id == blow.attacker; });
        const QString name = attacker != state.clients().end() ? (*attacker)->name : QString();
        QcGolemGrudges::noteBlow(state.world(), *mob, blow.attacker, name, state.info().worldTime);
    }
    const std::optional<QList<QcItemDrop>> drops = state.entities().creatures().damage(blow);
    if (drops) {
        state.entities().noteDeath(blow.creature);
        sounds.play(definition.deathSound, lootPosition);
        tellNamedDeath(state, named);
    } else if (take == QcHurtWindow::Take::Fresh) {
        sounds.play(definition.hurtSound, lootPosition);
    }

    if (drops) {
        // Like block drops (decision E-028) the loot falls where the mob died; only a grown
        // creature a player killed gives experience (decisions E-398, E-399).
        dropLoot(state, *drops, lootPosition);
        if (!young && blow.attacker != 0) {
            QcServerExperience::drop(state, QcServerExperience::roll(state, definition.experience), lootPosition);
        }
        for (const auto &client : state.clients()) {
            if (blow.attacker != 0 && client->id == blow.attacker) {
                state.advancements().trigger(*client, {.trigger = QcAdvancementTrigger::EntityKilled, .entity = definition.name});
                break;
            }
        }
    }
    return take != QcHurtWindow::Take::Ignored;
}

void QcServerCombat::tellNamedDeath(QcServerState &state, const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    for (const auto &client : state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            client->connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "%1 died"), {name}));
        }
    }
}

void QcServerCombat::dropLoot(QcServerState &state, const QList<QcItemDrop> &drops, const QcVec3 &position)
{
    for (const QcItemDrop &drop : drops) {
        if (drop.chance < 1.0 && state.random().generateDouble() >= drop.chance) {
            continue;
        }
        if (const std::optional<QcItemId> item = state.content().itemId(drop.item)) {
            state.entities().items().spawnScattered({*item, drop.count, 0}, position, state.entities().newId());
        }
    }
}
