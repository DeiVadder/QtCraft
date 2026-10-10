#include "QcServerInteraction.h"

#include "QcBlockRules.h"
#include "QcChatText.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcServerCombat.h"
#include "QcServerBlocks.h"
#include "QcServerInventory.h"
#include "QcServerMachines.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

#include <cmath>

using namespace Qt::StringLiterals;

QcServerInteraction::QcServerInteraction(QcServerState &state, const Parts &parts)
    : m_state(state)
    , m_parts(parts)
{
}

void QcServerInteraction::finishMining(QcServerClient &client, QcBlockPos pos)
{
    const bool sameTarget = client.miningPos && *client.miningPos == pos;
    client.miningPos.reset();

    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    if (!sameTarget || !block || *block == qcAirBlockId || !m_state.isWithinReach(client, pos) || !m_state.canSee(client, pos)) {
        // Resend the true block so a client that predicted the break shows it again.
        if (block) {
            client.connection->send(QcMsg::BlockUpdate{pos, *block});
        }
        return;
    }

    const QcBlockDefinition &definition = m_state.content->block(*block);
    const QcItemDefinition *held = m_state.heldItem(client);
    const double seconds = QcMining::breakSeconds(definition, held);
    const auto requiredTicks = quint64(std::ceil(seconds * QcPhysics::tickRate));
    if (seconds < 0.0 || m_state.tickCount - client.miningStartTick + miningToleranceTicks < requiredTicks) {
        client.connection->send(QcMsg::BlockUpdate{pos, *block});
        return;
    }

    // Drops fall into the world as items (decision E-028).
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
    if (!m_state.canSee(client, message.target)) {
        if (const std::optional<QcBlockId> clicked = m_state.world.blockAt(message.target)) {
            client.connection->send(QcMsg::BlockUpdate{message.target, *clicked});
        }
        rejectPlacement(client, qcNeighbour(message.target, message.face));
        return;
    }

    if (useClickedBlock(client, message.target)) {
        return;
    }

    const QcItemDefinition *held = m_state.heldItem(client);
    if (held && (held->scoops || held->bottles)) {
        if (!(held->bottles ? fillBottle(client, message.target) : scoopLiquid(client, message.target))) {
            if (const std::optional<QcBlockId> block = m_state.world.blockAt(message.target)) {
                client.connection->send(QcMsg::BlockUpdate{message.target, *block});
            }
            m_parts.inventory.send(client);
        }
        return;
    }

    placeHeld(client, message, held);
}

bool QcServerInteraction::useClickedBlock(QcServerClient &client, QcBlockPos target)
{
    // Using a machine opens its screen, a crafting table its 3 x 3 grid, unless the player sneaks
    // to place against them.
    const std::optional<QcBlockId> clickedBlock = m_state.world.blockAt(target);
    const bool storage = clickedBlock && m_state.containers.slotsOf(*clickedBlock) > 0;

    if (clickedBlock && (m_state.machines.machineOf(*clickedBlock) || storage) && !client.sneaking && m_state.isWithinReach(client, target)) {
        m_parts.machines.open(client, target);
        return true;
    }

    if (clickedBlock && m_state.content->block(*clickedBlock).opensCrafting && !client.sneaking && m_state.isWithinReach(client, target)) {
        m_parts.inventory.openCraftingTable(client, target);
        return true;
    }

    if (clickedBlock && m_state.content->block(*clickedBlock).toggles && !client.sneaking && m_state.isWithinReach(client, target)) {
        m_parts.blocks.toggle(target, *clickedBlock);
        return true;
    }

    return false;
}

void QcServerInteraction::placeHeld(QcServerClient &client, const QcMsg::UseItem &message, const QcItemDefinition *held)
{
    const QcContentRegistry &content = *m_state.content;
    const std::optional<QcBlockId> clicked = m_state.world.blockAt(message.target);
    const std::optional<QcBlockId> placed = held ? content.blockId(held->placesBlock) : std::nullopt;
    const bool clickedSolid = clicked && content.isValidBlock(*clicked) && !content.block(*clicked).liquid;
    const std::optional<QcBlockRules::Placement> placement =
        placed && clickedSolid && *clicked != qcAirBlockId
            ? QcBlockRules::placement(m_state.world, *placed, {message.target, message.face, message.hit, client.yaw})
            : std::nullopt;

    const QcBlockPos target = placement ? placement->pos : qcNeighbour(message.target, message.face);
    const std::optional<QcBlockId> existing = m_state.world.blockAt(target);
    if (!placement || !existing || !m_state.isWithinReach(client, target) || !mayPlace(client, placement->state, *placed, target)) {
        rejectPlacement(client, target);
        return;
    }

    applyPlacement(client, *held, placement->state, target);
}

bool QcServerInteraction::mayPlace(QcServerClient &client, QcBlockId state, QcBlockId placed, QcBlockPos target) const
{
    const QcContentRegistry &content = *m_state.content;
    // Saplings need suitable ground.
    const QcBlockDefinition &placedDefinition = content.block(state);
    const std::optional<QcBlockId> ground = m_state.world.blockAt(target.offset(0, -1, 0));
    if (placedDefinition.sapling.isSapling() && !m_state.plants.canPlant(placed, ground)) {
        return false;
    }

    if (placedDefinition.containerSlots > 0 && m_state.containers.isFull()) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many chests in the world (at most %1)"),
            {QString::number(QcContainerManager::maxContainers)}));
        return false;
    }

    if (placedDefinition.machine.isMachine() && m_state.machines.isFull()) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many machines in the world (at most %1)"),
            {QString::number(QcMachineManager::maxMachines)}));
        return false;
    }

    // Nobody, player or mob, may end up inside what is placed (both halves of a door).
    QList<QcAabb> boxes;
    const QcVec3 corner{double(target.x), double(target.y), double(target.z)};
    for (const QcAabb &box : content.state(state).collision) {
        boxes.append(box.moved(corner));
        if (placedDefinition.door) {
            boxes.append(box.moved(corner + QcVec3{0.0, 1.0, 0.0}));
        }
    }

    return !m_parts.blocks.overlapsBody(boxes);
}

void QcServerInteraction::applyPlacement(QcServerClient &client, const QcItemDefinition &held, QcBlockId state, QcBlockPos target)
{
    const QcContentRegistry &content = *m_state.content;
    const QcBlockDefinition &placedDefinition = content.block(state);
    m_parts.blocks.setAndBroadcast(target, state);
    if (placedDefinition.door) {
        m_parts.blocks.setAndBroadcast(target.offset(0, 1, 0), content.withProperty(state, u"half"_s, u"top"_s));
    }

    m_parts.sounds.playBlock(placedDefinition, QcBlockSoundAction::Place, target);
    if (placedDefinition.sapling.isSapling()) {
        m_state.plants.saplingPlaced(target, state);
    }
    if (placedDefinition.machine.isMachine()) {
        m_state.machines.add(target);
    }
    if (placedDefinition.containerSlots > 0) {
        m_state.containers.add(target, placedDefinition.containerSlots);
    }

    if (!held.emptiesTo.isEmpty() && content.itemId(held.emptiesTo)) {
        // A poured bucket stays in the hand, empty.
        m_parts.sounds.play(QString::fromLatin1(QcSoundKeys::bucketEmpty), {target.x + 0.5, target.y + 0.5, target.z + 0.5});
        m_parts.inventory.exchangeHeld(client, held.emptiesTo);
    } else {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_parts.inventory.send(client);
}

void QcServerInteraction::rejectPlacement(QcServerClient &client, QcBlockPos target)
{
    for (const QcBlockPos pos : {target, target.offset(0, 1, 0)}) {
        if (const std::optional<QcBlockId> block = m_state.world.blockAt(pos)) {
            client.connection->send(QcMsg::BlockUpdate{pos, *block});
        }
    }
    m_parts.inventory.send(client);
}

bool QcServerInteraction::scoopLiquid(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    if (!block || !m_state.content->isValidBlock(*block) || !m_state.isWithinReach(client, pos)) {
        return false;
    }

    const QcBlockDefinition &definition = m_state.content->block(*block);
    if (!definition.liquid || definition.flow.bucket.isEmpty() || !QcLiquidFlow::isSource(*m_state.content, *block)
        || !m_state.content->itemId(definition.flow.bucket)) {
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
    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    if (!block || !m_state.content->isValidBlock(*block) || !m_state.isWithinReach(client, pos)) {
        return false;
    }

    const QcBlockDefinition &definition = m_state.content->block(*block);
    if (!definition.liquid || definition.flow.bottle.isEmpty() || !m_state.content->itemId(definition.flow.bottle)) {
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
    const QcMobManager::Mob *mob = m_state.mobs.find(entityId);
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!mob || !held) {
        return;
    }

    const QcMobDefinition &definition = m_state.content->mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if (definition.catchWith != held->name || !m_state.content->itemId(definition.catchItem)
        || (centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0
        || !m_state.canSee(client, QcPhysics::boxAt(mob->motion.position, definition.width, definition.height))) {
        return;
    }

    m_state.mobs.remove(entityId);
    m_parts.sounds.play(QString::fromLatin1(QcSoundKeys::bottleCatch), centre);
    m_parts.inventory.exchangeHeld(client, definition.catchItem);
    m_parts.inventory.send(client);
}
