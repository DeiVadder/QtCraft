#include "server/gameplay/QcServerInteraction.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/blockentities/QcServerSigns.h"
#include "server/blocks/QcBlockDyeing.h"
#include "server/blocks/QcBlockRules.h"
#include "server/blocks/QcFireIgnition.h"
#include "server/blocks/QcOpeners.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerCrafting.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcServerStands.h"
#include "server/golems/QcServerGolems.h"
#include "server/hanging/QcServerHanging.h"
#include "server/machines/QcBlockScreens.h"
#include "server/machines/QcServerKitchen.h"
#include "server/machines/QcServerStonecutter.h"
#include "server/minecarts/QcServerMinecarts.h"
#include "server/mobs/QcServerAnimals.h"
#include "server/plants/QcServerPlants.h"
#include "server/signal/QcServerCircuits.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcServerInteraction::QcServerInteraction(QcServerState &state, const Parts &parts)
    : m_state(state)
    , m_parts(parts)
{
}

void QcServerInteraction::finishMining(QcServerClient &client, QcBlockPos pos)
{
    const bool sameTarget = client.mining.pos && *client.mining.pos == pos;
    client.mining.pos.reset();

    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!sameTarget || !block || *block == qcAirBlockId || !QcLineOfSight::isWithinReach(client, pos) || !QcLineOfSight::canSee(m_state.world(), client, pos)
        || !permits(client, QcAdventureRules::Action::Break, pos)) {
        // Resend the true block so a client that predicted the break shows it again.
        if (block) {
            client.connection->send(QcMsg::BlockUpdate{pos, *block});
        }
        return;
    }

    const QcBlockDefinition &definition = m_state.content().block(*block);
    const QcItemDefinition *held = m_state.heldItem(client);
    // Shears (the item that carves) cut a tripwire without setting its hooks off (decision E-782);
    // only a break that happens disarms it.
    const auto disarmCutWire = [&] {
        if (held && held->carves && definition.signal.kind == QcSignalKind::Tripwire) {
            m_state.world().setBlock(pos, m_state.content().withProperty(*block, u"disarmed"_s, u"true"_s));
        }
    };

    if (client.rules().instantBreak) {
        // Creative mode: every block breaks at once, without drops or wear (decision E-282).
        disarmCutWire();
        m_parts.blocks.breakBlock(pos, false);
        return;
    }

    const double seconds = QcMining::breakSeconds(definition, held);
    // In the players' time, so the mining takes as long at every tick rate.
    constexpr qint64 tickNs = qint64(QcPhysics::tickDuration * 1e9);
    const qint64 requiredNs = qint64(std::ceil(seconds * QcPhysics::tickRate)) * tickNs;
    const qint64 toleranceNs = (miningToleranceTicks - 1) * tickNs + std::max(tickNs, m_state.clock().lastTickNs());
    if (seconds < 0.0 || m_state.clock().playerTimeNs() - client.mining.startNs + toleranceNs < requiredNs) {
        client.connection->send(QcMsg::BlockUpdate{pos, *block});
        return;
    }

    // Drops fall into the world as items (decision E-028).
    disarmCutWire();
    m_parts.blocks.breakBlock(pos, QcMining::canHarvest(definition, held));
    client.hunger.exhaust(QcHunger::mineBlock);

    if (held && held->durability > 0) {
        const int slot = client.inventory.selectedHotbarSlot();
        QcItemStack tool = client.inventory.slot(slot);
        if (++tool.damage >= held->durability) {
            tool = {};
        }
        client.inventory.setSlot(slot, tool);
    }

    m_parts.inventory.send(client);
}

void QcServerInteraction::useItem(QcServerClient &client, const QcMsg::UseItem &message)
{
    client.swung = true;

    // Nothing is used, placed or scooped through walls; the client may have predicted a placement.
    if (!QcLineOfSight::canSee(m_state.world(), client, message.target)) {
        if (const std::optional<QcBlockId> clicked = m_state.world().blockAt(message.target)) {
            client.connection->send(QcMsg::BlockUpdate{message.target, *clicked});
        }
        rejectPlacement(client, qcNeighbour(message.target, message.face));
        return;
    }

    if (useClickedBlock(client, message.target)) {
        return;
    }

    const QcItemDefinition *held = m_state.heldItem(client);
    // Adventure mode (decision E-735): the held item acts on the block only where the world's
    // allow-lists permit it; food that would plant is eaten instead.
    if (!permits(client, QcAdventureRules::Action::PlaceOn, message.target)) {
        if (const std::optional<QcBlockId> clicked = m_state.world().blockAt(message.target)) {
            client.connection->send(QcMsg::BlockUpdate{message.target, *clicked});
        }
        rejectPlacement(client, qcNeighbour(message.target, message.face));
        if (held && held->food > 0) {
            m_parts.health.consume(client);
        }
        return;
    }

    if (held && (held->scoops || held->bottles)) {
        if (!(held->bottles ? fillBottle(client, message.target) : scoopLiquid(client, message.target))) {
            if (const std::optional<QcBlockId> block = m_state.world().blockAt(message.target)) {
                client.connection->send(QcMsg::BlockUpdate{message.target, *block});
            }
            m_parts.inventory.send(client);
        }
        return;
    }

    const bool reshaped = m_parts.plants.till(client, message.target, message.face, held)
                          || m_parts.plants.flatten(client, message.target, message.face, held)
                          || m_parts.plants.fertilize(client, message.target, held)
                          || m_parts.plants.carve(client, message.target, message.face, held)
                          || m_parts.plants.strip(client, message.target, held)
                          || QcBlockDyeing(m_state, m_parts.blocks, m_parts.sounds, m_parts.inventory).dye(client, message.target, held);
    if (reshaped || m_parts.ignition.use(client, message) || m_parts.stands.place(client, message) || m_parts.minecarts.place(client, message)
        || QcServerHanging(m_state, m_parts.sounds, m_parts.inventory).place(client, message)) {
        return;
    }
    placeHeld(client, message, held);
}

bool QcServerInteraction::permits(const QcServerClient &client, QcAdventureRules::Action action, QcBlockPos pos) const
{
    if (!client.rules().buildByAllowList) {
        return true;
    }

    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block)) {
        return false;
    }
    return m_state.info().adventure.permits(client.gameMode, action, m_state.heldItem(client), m_state.content().block(*block));
}

bool QcServerInteraction::useClickedBlock(QcServerClient &client, QcBlockPos target)
{
    // Using a machine opens its screen, a crafting table its 3 x 3 grid, unless the player sneaks
    // to place against them.
    const std::optional<QcBlockId> clickedBlock = m_state.world().blockAt(target);
    const bool storage = clickedBlock && m_state.blockEntities().containerSlots(*clickedBlock) > 0;

    if (clickedBlock && (m_state.machineRules().machineOf(*clickedBlock) || storage) && !client.sneaking && QcLineOfSight::isWithinReach(client, target)) {
        m_parts.screens.open(client, target);
        return true;
    }

    if (clickedBlock && m_state.content().block(*clickedBlock).opensCrafting && !client.sneaking && QcLineOfSight::isWithinReach(client, target)) {
        m_parts.crafting.openCraftingTable(client, target);
        return true;
    }

    // The stonecutter (decision E-785).
    if (clickedBlock && !client.sneaking && QcLineOfSight::isWithinReach(client, target) && m_parts.stonecutter.open(client, target)) {
        return true;
    }

    if (clickedBlock && m_state.content().block(*clickedBlock).toggles && !client.sneaking && QcLineOfSight::isWithinReach(client, target)) {
        QcOpeners(m_state, m_parts.sounds, m_parts.blocks).toggle(target, *clickedBlock);
        return true;
    }

    // A dye colours a sign's text, else the sign opens its editor (decisions E-830, E-766).
    if (clickedBlock && !client.sneaking
        && (QcBlockDyeing(m_state, m_parts.blocks, m_parts.sounds, m_parts.inventory).dyeSign(client, target, m_state.heldItem(client))
            || QcServerSigns(m_state).openEditor(client, target))) {
        return true;
    }

    // Cake and campfires (decisions E-772, E-773).
    if (clickedBlock && !client.sneaking && QcLineOfSight::isWithinReach(client, target) && m_parts.kitchen.use(client, target)) {
        return true;
    }

    // Levers, buttons and repeaters (decision E-339); a spammed switch is ignored beyond its budget.
    if (clickedBlock && m_state.content().block(*clickedBlock).signal.isUsable() && !client.sneaking && QcLineOfSight::isWithinReach(client, target)) {
        if (client.input.rateLimiter.allow(QcRateLimiter::Kind::Switch, m_state.clock().playerTimeNs())) {
            m_parts.circuits.use(target);
        }
        return true;
    }

    return false;
}

void QcServerInteraction::placeHeld(QcServerClient &client, const QcMsg::UseItem &message, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(message.target);
    const std::optional<QcBlockId> placed = held ? content.blockId(held->placesBlock) : std::nullopt;
    const bool clickedLiquid = clicked && content.isValidBlock(*clicked) && content.block(*clicked).liquid;
    // A floating block (a lily pad, decision E-774) goes onto the surface of the liquid it was aimed
    // at, never into a liquid further down a column.
    const std::optional<QcBlockId> above = m_state.world().blockAt(message.target.offset(0, 1, 0));
    const bool surface = above && content.isValidBlock(*above) && !content.block(*above).liquid;
    const bool floating = placed && clickedLiquid && surface && content.block(*placed).floats;
    const QcBlockFace face = floating ? QcBlockFace::Up : message.face;
    const bool clickedSolid = clicked && content.isValidBlock(*clicked) && !clickedLiquid;
    const std::optional<QcBlockRules::Placement> placement =
        placed && (clickedSolid || floating) && *clicked != qcAirBlockId
            ? QcBlockRules::placement(m_state.world(), *placed, {message.target, face, message.hit, client.yaw, client.pitch})
            : std::nullopt;

    const QcBlockPos target = placement ? placement->pos : qcNeighbour(message.target, face);
    const std::optional<QcBlockId> existing = m_state.world().blockAt(target);
    // A fish bucket whose fish may not come out (decision E-501) pours nothing.
    if (!placement || !existing || !QcLineOfSight::isWithinReach(client, target) || !mayPlace(client, placement->state, *placed, target)
        || !QcServerAnimals::mayRelease(m_state, *held, QcServerAnimals::releasePoint(target))) {
        rejectPlacement(client, target);
        // Food that plants (carrots, potatoes) is eaten where it cannot be planted (decision E-292).
        if (held && held->food > 0) {
            m_parts.health.consume(client);
        }
        return;
    }

    applyPlacement(client, *held, placement->state, target);
}

bool QcServerInteraction::mayPlace(QcServerClient &client, QcBlockId state, QcBlockId placed, QcBlockPos target) const
{
    const QcContentRegistry &content = m_state.content();
    // Saplings need suitable ground.
    const QcBlockDefinition &placedDefinition = content.block(state);
    const std::optional<QcBlockId> ground = m_state.world().blockAt(target.offset(0, -1, 0));
    if (placedDefinition.sapling.isSapling() && !m_state.plants().canPlant(placed, ground)) {
        return false;
    }

    if (placedDefinition.containerSlots > 0 && m_state.blockEntities().isContainerTableFull()) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many chests in the world (at most %1)"),
            {QString::number(m_state.blockEntities().limits().containers)}));
        return false;
    }

    // A player's chunk loaders are limited (decision E-377).
    const qint64 loaderLimit = m_state.config().limits.chunkLoadersPerPlayer;
    if (m_state.world().loaders().isLoader(state) && m_state.world().loaders().countOf(client.name) >= loaderLimit) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "You have placed as many chunk loaders as you may (%1)"),
            {QString::number(loaderLimit)}));
        return false;
    }

    if (placedDefinition.machine.isMachine() && m_state.blockEntities().isMachineTableFull()) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many machines in the world (at most %1)"),
            {QString::number(m_state.blockEntities().limits().machines)}));
        return false;
    }

    // Nobody, player or mob, may end up inside what is placed (both halves of a door or bed).
    QList<QcAabb> boxes;
    const QcVec3 corner{double(target.x), double(target.y), double(target.z)};
    const QcBlockPos other = QcBlockRules::otherHalf(m_state.world(), target, state);
    for (const QcAabb &box : content.state(state).collision) {
        boxes.append(box.moved(corner));
        if (placedDefinition.door || placedDefinition.bed) {
            boxes.append(box.moved(corner + QcVec3{double(other.x - target.x), double(other.y - target.y), double(other.z - target.z)}));
        }
    }

    return !m_state.entities().overlapsLiving(boxes);
}

void QcServerInteraction::applyPlacement(QcServerClient &client, const QcItemDefinition &held, QcBlockId state, QcBlockPos target)
{
    const QcContentRegistry &content = m_state.content();
    const QcBlockDefinition &placedDefinition = content.block(state);
    m_parts.blocks.setAndBroadcast(target, state);
    if (placedDefinition.door) {
        m_parts.blocks.setAndBroadcast(target.offset(0, 1, 0), content.withProperty(state, u"half"_s, u"top"_s));
    }
    if (placedDefinition.bed) {
        m_parts.blocks.setAndBroadcast(QcBlockRules::otherHalf(m_state.world(), target, state), content.withProperty(state, u"part"_s, u"head"_s));
    }

    // Setting the block made its chest, machine or sapling timer (QcServerBlocks::setQuietly); a chunk
    // loader belongs to the player who placed it.
    m_state.world().loaders().setOwner(target, client.name);
    m_parts.sounds.playBlock(placedDefinition, QcBlockSoundAction::Place, target);
    // A placed sign asks for its text (decision E-766).
    QcServerSigns(m_state).openEditor(client, target);
    // A golem's head on its body brings it to life (decision E-471).
    m_parts.golems.build(target);
    // A fish bucket lets its fish out into the water it pours, kept with the world (decision E-501).
    QcServerAnimals::release(m_state, held, QcServerAnimals::releasePoint(target));

    const bool pours = !held.emptiesTo.isEmpty() && content.itemId(held.emptiesTo);
    if (pours) {
        m_parts.sounds.play(QString::fromLatin1(QcSoundKeys::bucketEmpty), {target.x + 0.5, target.y + 0.5, target.z + 0.5});
    }
    // In creative mode the held stack (a full bucket too) stays as it is (decision E-282).
    if (pours && !client.rules().infiniteItems) {
        // A poured bucket stays in the hand, empty.
        m_parts.inventory.exchangeHeld(client, held.emptiesTo);
    } else if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_parts.inventory.send(client);
}

void QcServerInteraction::rejectPlacement(QcServerClient &client, QcBlockPos target)
{
    for (const QcBlockPos pos : {target, target.offset(0, 1, 0)}) {
        if (const std::optional<QcBlockId> block = m_state.world().blockAt(pos)) {
            client.connection->send(QcMsg::BlockUpdate{pos, *block});
        }
    }
    m_parts.inventory.send(client);
}

bool QcServerInteraction::scoopLiquid(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block) || !QcLineOfSight::isWithinReach(client, pos)) {
        return false;
    }

    const QcBlockDefinition &definition = m_state.content().block(*block);
    if (!definition.liquid || definition.flow.bucket.isEmpty() || !QcLiquidFlow::isSource(m_state.content(), *block)
        || !m_state.content().itemId(definition.flow.bucket)) {
        return false;
    }

    m_parts.blocks.setAndBroadcast(pos, qcAirBlockId);
    m_parts.sounds.play(QString::fromLatin1(QcSoundKeys::bucketFill), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    m_parts.inventory.exchangeHeld(client, definition.flow.bucket);
    m_parts.inventory.send(client);
    return true;
}

bool QcServerInteraction::fillBottle(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !m_state.content().isValidBlock(*block) || !QcLineOfSight::isWithinReach(client, pos)) {
        return false;
    }

    const QcBlockDefinition &definition = m_state.content().block(*block);
    if (!definition.liquid || definition.flow.bottle.isEmpty() || !m_state.content().itemId(definition.flow.bottle)) {
        return false;
    }

    m_parts.sounds.play(QString::fromLatin1(QcSoundKeys::bucketFill), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    m_parts.inventory.exchangeHeld(client, definition.flow.bottle);
    m_parts.inventory.send(client);
    return true;
}

void QcServerInteraction::useEntity(QcServerClient &client, quint32 entityId)
{
    client.swung = true;
    // Tameable creatures first: taming, healing, collars and sitting (decision E-480).
    if (m_parts.animals.tend(client, entityId)) {
        return;
    }
    // A cow takes the bucket before anything else is done with it (decision E-500).
    if (m_parts.animals.milk(client, entityId)) {
        return;
    }
    // Food the animal eats is never eaten by the player instead, even when the animal refuses it now.
    if (m_parts.animals.feed(client, entityId) != QcServerAnimals::Feeding::NotItsFood) {
        return;
    }

    const QcCreature *mob = m_state.entities().creatures().find(entityId);
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!mob || !held || m_parts.golems.repair(client, entityId, held)) {
        return;
    }

    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if (definition.catchWith != held->name || !m_state.content().itemId(definition.catchItem)
        || (centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0
        || !QcLineOfSight::canSee(m_state.world(), client, QcServerAnimals::shownBox(definition, *mob))) {
        // Food and armour aimed at a creature that does not take them are eaten or put on.
        m_parts.health.consume(client);
        return;
    }

    m_state.entities().creatures().remove(entityId);
    m_parts.sounds.play(definition.catchSound, centre);
    m_parts.inventory.exchangeHeld(client, definition.catchItem);
    m_parts.inventory.send(client);
}

void QcServerInteraction::swingWhileMining()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && client->mining.pos
            && (m_state.clock().tick() - client->mining.startTick) % miningSwingTicks == 0) {
            client->swung = true;
        }
    }
}
