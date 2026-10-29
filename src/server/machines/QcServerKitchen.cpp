// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcServerKitchen.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerInventory.h"

using namespace Qt::StringLiterals;

QcServerKitchen::QcServerKitchen(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory,
                                 QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
    , m_health(health)
{
}

bool QcServerKitchen::use(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || client.dead) {
        return false;
    }

    const QcBlockDefinition &definition = m_state.content().block(*block);
    if (definition.bites.isEdible()) {
        return eatBite(client, pos, *block);
    }
    if (definition.campfire.cooks()) {
        return putOut(client, pos, *block) || putOnCampfire(client, pos, *block);
    }
    return false;
}

bool QcServerKitchen::eatBite(QcServerClient &client, QcBlockPos pos, QcBlockId block)
{
    // Full players and players whose hunger stands still (creative mode) leave the cake as it is; the
    // use is still the cake's, so nothing is placed against it.
    const QcBlockDefinition &definition = m_state.content().block(block);
    if (client.hunger.frozen() || !client.hunger.eat(definition.bites.food)) {
        return true;
    }

    const QcBlockProperty *bites = definition.property(u"bites"_s);
    const qsizetype taken = bites ? bites->values.indexOf(m_state.content().property(block, u"bites"_s)) : -1;
    const bool last = !bites || taken < 0 || taken + 1 >= bites->values.size();
    m_blocks.setAndBroadcast(pos, last ? qcAirBlockId : m_state.content().withProperty(block, u"bites"_s, bites->values[taken + 1]));
    m_sounds.play(QString::fromLatin1(QcSoundKeys::playerEat), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
    m_health.sendFood(client);
    return true;
}

bool QcServerKitchen::putOnCampfire(QcServerClient &client, QcBlockPos pos, QcBlockId block)
{
    const QcItemStack held = client.inventory.slot(client.inventory.selectedHotbarSlot());
    const QcCampfireDefinition &campfire = m_state.content().block(block).campfire;
    if (held.isEmpty() || !m_state.machineRules().recipeFor(campfire.category, campfire.kinds, held.item)) {
        return false;
    }

    // A full campfire takes nothing more; the use is still the campfire's (the food is not eaten).
    if (m_state.world().campfires().put(pos, campfire.slotCount, held.item) && !client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.playBlock(m_state.content().block(block), QcBlockSoundAction::Place, pos);
    m_inventory.send(client);
    return true;
}

bool QcServerKitchen::putOut(QcServerClient &client, QcBlockPos pos, QcBlockId block)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    const std::optional<QcBlockId> out = m_state.content().blockId(m_state.content().block(block).campfire.extinguishesInto);
    if (!held || !out || !putsOut(*held)) {
        return false;
    }
    // Putting it out changes the block: in adventure only where the allow-lists permit the held item
    // on it (decision E-735); cooking on it changes no block and stays allowed.
    if (!m_state.info().adventure.permits(client.gameMode, QcAdventureRules::Action::PlaceOn, held, m_state.content().block(block))) {
        return false;
    }

    // The items on it drop (QcCampfires::noteBlock); a bucket is poured out, a shovel wears.
    m_blocks.setAndBroadcast(pos, *out);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::liquidHiss), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    if (held->placesBlock.isEmpty()) {
        m_inventory.wearHeld(client);
    } else if (!client.rules().infiniteItems) {
        m_inventory.exchangeHeld(client, held->emptiesTo);
    }
    m_inventory.send(client);
    return true;
}

bool QcServerKitchen::putsOut(const QcItemDefinition &held) const
{
    if (held.tool == u"shovel"_s) {
        return true;
    }
    const std::optional<QcBlockId> poured = held.placesBlock.isEmpty() ? std::nullopt : m_state.content().blockId(held.placesBlock);
    return poured && !held.emptiesTo.isEmpty() && m_state.content().block(*poured).liquid && m_state.content().block(*poured).contactDamage <= 0.0;
}

bool QcServerKitchen::light(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    const std::optional<QcBlockId> lit = block ? m_state.content().blockId(m_state.content().block(*block).campfire.lightsInto) : std::nullopt;
    if (!lit) {
        return false;
    }
    m_blocks.setAndBroadcast(pos, *lit);
    return true;
}

void QcServerKitchen::tick()
{
    if (m_state.world().campfires().size() == 0) {
        return;
    }
    const QList<QcBlockPos> campfires = m_state.world().campfires().positions();
    for (const QcBlockPos &pos : campfires) {
        cook(pos);
    }
    announceChanges();
}

void QcServerKitchen::cook(QcBlockPos pos)
{
    const QcCampfires::Entry *entry = m_state.world().campfires().at(pos);
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!entry || !block || !m_state.isLoadedAt(pos)) {
        return;
    }

    const QcCampfireDefinition &campfire = m_state.content().block(*block).campfire;
    m_state.world().campfires().addCookingTime(pos, QcPhysics::tickDuration);
    QcCampfires::Entry next = *entry;
    QList<QcItemStack> done;
    for (QcCampfires::Slot &slot : next.places) {
        if (slot.stack.isEmpty()) {
            continue;
        }
        const QcSmeltingRecipe *recipe = m_state.machineRules().recipeFor(campfire.category, campfire.kinds, slot.stack.item);
        const std::optional<QcItemId> result = recipe ? m_state.content().itemId(recipe->resultItem) : std::nullopt;
        // An item without a recipe any more (a mod went) comes off as it is.
        if (!result || slot.cooked >= campfire.seconds) {
            done.append(result ? QcItemStack{*result, recipe->resultCount, 0} : slot.stack);
            slot = {};
        }
    }
    // Only a finished item changes the entry; the cooking time was added in place.
    if (done.isEmpty()) {
        return;
    }
    m_state.world().campfires().set(next);
    m_blocks.dropStacks(done, pos);
}

void QcServerKitchen::announceChanges()
{
    const QList<QcBlockPos> changes = m_state.world().campfires().takeChanges();
    for (const QcBlockPos &pos : changes) {
        const QcMsg::CampfireDisplay display = displayOf(m_state.world(), pos);
        for (const auto &client : m_state.clients()) {
            if (client->state == QcServerClient::State::Playing && client->view.sentChunks.contains(pos.chunk())) {
                client->connection->send(display);
            }
        }
    }
}

QcMsg::CampfireDisplay QcServerKitchen::displayOf(const QcServerWorld &world, QcBlockPos pos)
{
    QcMsg::CampfireDisplay display{pos, {}};
    if (const QcCampfires::Entry *entry = world.campfires().at(pos)) {
        for (const QcCampfires::Slot &slot : entry->places) {
            display.items.append(slot.stack.isEmpty() ? qcNoItem : slot.stack.item);
        }
    }
    return display;
}
