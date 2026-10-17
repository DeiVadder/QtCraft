#include "server/gameplay/QcServerCombat.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/items/QcServerExperience.h"

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

    const QcCreature *mob = m_state.entities().creatures().find(entityId);
    if (!mob) {
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
    client.hunger.exhaust(QcHunger::attack);

    const QcItemDefinition *held = m_state.heldItem(client);
    const double damage = held ? held->attackDamage : 1.0;
    const QcVec3 lootPosition = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    // Young animals give neither loot nor experience (like the reference); read before the hit
    // removes a dead creature.
    const bool young = mob->isYoung();

    // Hits during the short invulnerability after a hit do nothing, and make no sound.
    const bool vulnerable = mob->hurtTimer <= 0.0;
    const std::optional<QList<QcItemDrop>> drops = m_state.entities().creatures().damage(entityId, damage, client.motion.position);
    if (drops) {
        m_state.entities().noteDeath(entityId);
        m_sounds.play(definition.deathSound, lootPosition);
    } else if (vulnerable) {
        m_sounds.play(definition.hurtSound, lootPosition);
    }

    // Weapons wear like tools, except in creative mode (decision E-282).
    if (held && held->durability > 0 && !client.rules().infiniteItems) {
        const int slot = client.inventory.selectedHotbarSlot();
        QcItemStack tool = client.inventory.slot(slot);
        if (++tool.damage >= held->durability) {
            tool = {};
        }
        client.inventory.setSlot(slot, tool);
        m_inventory.send(client);
    }

    if (drops) {
        // Like block drops (decision E-028) the loot falls where the mob died; only a grown
        // creature a player killed gives experience (decisions E-398, E-399).
        dropLoot(m_state, *drops, lootPosition);
        if (!young) {
            QcServerExperience::drop(m_state, QcServerExperience::roll(m_state, definition.experience), lootPosition);
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
