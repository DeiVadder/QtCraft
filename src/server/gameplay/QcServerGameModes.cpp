#include "server/gameplay/QcServerGameModes.h"

#include "common/gameplay/QcArmour.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCrafting.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcSpectatorRules.h"
#include "server/minecarts/QcServerMinecarts.h"

#include <algorithm>
#include <utility>

QcServerGameModes::QcServerGameModes(QcServerState &state, QcServerInventory &inventory, QcServerCrafting &crafting, QcServerMinecarts &minecarts)
    : m_state(state)
    , m_inventory(inventory)
    , m_crafting(crafting)
    , m_minecarts(minecarts)
{
}

QcGameMode QcServerGameModes::defaultMode() const
{
    return m_state.config().defaultGameMode.value_or(m_state.info().defaultGameMode);
}

void QcServerGameModes::setMode(QcServerClient &client, QcGameMode mode)
{
    if (QcGameModes::inventorySet(client.gameMode) != QcGameModes::inventorySet(mode)) {
        // The items of the grid and the stonecutter belong to the inventory that is put aside.
        m_crafting.returnCraftingGrid(client);
        m_inventory.returnStonecutterInput(client);
        std::swap(client.inventory, client.keptInventory);
        const QcEquipment worn = client.keptEquipment;
        client.keptEquipment = client.equipment;
        m_inventory.setEquipment(client, worn);
    }

    const bool leavesBlocks = client.rules().noClip && !QcGameModes::rules(mode).noClip;
    client.gameMode = mode;
    applyRules(client);
    if (leavesBlocks) {
        comeOutOfBlocks(client);
    }
    if (!client.rules().tangible) {
        // A spectator rides nothing (decision E-730).
        m_minecarts.leave(client);
    }
    // The new rules may end a flight; the client hears it with the next state. The others see a
    // spectator vanish or come back with the next sync, and the list marks him (decision E-730).
    client.stateDirty = true;
    client.mining.pos.reset();
    m_state.notePlayerListChanged();
    send(client, m_state.info().adventure);
    m_inventory.send(client);
}

void QcServerGameModes::comeOutOfBlocks(QcServerClient &client) const
{
    // The flight's momentum ends with the mode, wherever the body is.
    client.motion.velocity = {};
    client.fallPeak = client.motion.position.y;

    const std::optional<QcVec3> free = QcSpectatorRules::freePlaceAbove(m_state.world(), client.motion.position);
    if (!free || *free == client.motion.position) {
        return;
    }

    qCDebug(lcGameModes) << "Moved" << client.name << "out of the blocks to" << free->y;
    client.motion.position = *free;
    client.previousPosition.reset();
    client.fallPeak = free->y;
}

void QcServerGameModes::applyRules(QcServerClient &client)
{
    const QcGameModes::Rules rules = QcGameModes::rules(client.gameMode);
    client.hunger.setFrozen(rules.hungerFrozen);
    if (!rules.mayFly) {
        client.motion.flying = false;
    }
    if (!rules.interacts) {
        // A spectator draws no launcher (decision E-730).
        client.drawing = false;
    }
}

void QcServerGameModes::send(QcServerClient &client, const QcAdventureRules &adventure)
{
    const bool listed = QcGameModes::rules(client.gameMode).buildByAllowList;
    client.connection->send(QcMsg::GameMode{client.gameMode, listed ? adventure : QcAdventureRules{}});
}

void QcServerGameModes::sendAdventureRules() const
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && QcGameModes::rules(client->gameMode).buildByAllowList) {
            send(*client, m_state.info().adventure);
        }
    }
}

QcItemStack QcServerGameModes::validStack(const QcItemStack &stack) const
{
    const QcContentRegistry &content = m_state.content();
    if (stack.count <= 0 || !content.isValidItem(stack.item)) {
        return {};
    }

    // Of the data only the map id of a filled map (decisions E-798, E-823): contents would mint
    // items past the rules of what a container holds, names are given at the anvil.
    const QcItemDefinition &definition = content.item(stack.item);
    const QcItemStack plain{stack.item, std::clamp(stack.count, 1, definition.maxStack), definition.keptDamage(stack.damage)};
    return definition.map == QcMapItem::Filled ? plain.withMapId(stack.mapId()) : plain;
}

void QcServerGameModes::creativeItem(QcServerClient &client, const QcMsg::CreativeItem &message)
{
    if (!QcGameModes::rules(client.gameMode).itemCatalogue || client.dead) {
        // A client out of step with its mode (or a cheat): it gets the true inventory back.
        qCDebug(lcGameModes) << "Refused a creative item for" << client.name << "in" << QcGameModes::name(client.gameMode);
        m_inventory.send(client);
        return;
    }

    // The slot of a backpack open in the hand is locked (decision E-833).
    if (client.screen.held && message.slot == client.screen.held->slot) {
        m_inventory.send(client);
        return;
    }

    const QcItemStack stack = validStack(message.stack);
    if (message.slot == QcMsg::CreativeItem::addSlot) {
        if (!stack.isEmpty()) {
            client.inventory.add(stack, m_state.content());
        }
    } else if (QcMsg::MoveInventoryItem::isEquipmentSlot(message.slot)) {
        putIntoArmourSlot(client, message.slot - QcMsg::MoveInventoryItem::equipmentSlot, stack);
    } else if (QcInventory::isValidSlot(message.slot)) {
        putIntoSlot(client, message.slot, stack);
    }
    m_inventory.send(client);
}

void QcServerGameModes::putIntoSlot(QcServerClient &client, int slot, const QcItemStack &stack) const
{
    const QcItemStack existing = client.inventory.slot(slot);
    if (!stack.isEmpty() && !existing.isEmpty() && existing.stacksWith(stack)) {
        const int limit = m_state.content().item(stack.item).maxStack;
        client.inventory.setSlot(slot, stack.withCount(std::min(existing.count + stack.count, limit)));
        return;
    }
    client.inventory.setSlot(slot, stack);
}

bool QcServerGameModes::putIntoArmourSlot(QcServerClient &client, int slot, const QcItemStack &stack) const
{
    if (!stack.isEmpty() && !QcArmour::fits(m_state.content(), stack, QcEquipmentSlot(slot))) {
        return false;
    }

    QcEquipment worn = client.equipment;
    worn.stacks[std::size_t(slot)] = stack.isEmpty() ? QcItemStack{} : stack.withCount(1);
    m_inventory.setEquipment(client, worn);
    return true;
}

std::optional<QcItemId> QcServerGameModes::itemForBlock(const QcContentRegistry &content, QcBlockId block)
{
    if (!content.isValidBlock(block)) {
        return std::nullopt;
    }

    const QcBlockDefinition &definition = content.block(block);
    if (const std::optional<QcItemId> own = content.itemId(definition.name)) {
        return own;
    }

    const QList<QcItemDefinition> &items = content.items();
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (items[i].placesBlock == definition.name) {
            return QcItemId(i + 1);
        }
    }

    for (const QcItemDrop &drop : definition.drops) {
        if (const std::optional<QcItemId> dropped = content.itemId(drop.item)) {
            return dropped;
        }
    }
    return std::nullopt;
}

int QcServerGameModes::pickTarget(const QcServerClient &client)
{
    const int selected = client.inventory.selectedHotbarSlot();
    if (client.inventory.slot(selected).isEmpty()) {
        return selected;
    }

    for (int slot = 0; slot < QcInventory::hotbarSize; ++slot) {
        if (client.inventory.slot(slot).isEmpty()) {
            return slot;
        }
    }
    return selected;
}

void QcServerGameModes::pickBlock(QcServerClient &client, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    // Like mining and placing: within reach and in sight, not through walls.
    if (client.dead || !block || *block == qcAirBlockId || !QcLineOfSight::isWithinReach(client, pos) || !QcLineOfSight::canSee(m_state.world(), client, pos)) {
        return;
    }
    // A backpack open in the hand keeps its slot and the selection (decision E-833): picking would
    // swap or overwrite the slot it is locked in.
    if (client.screen.held) {
        m_inventory.send(client);
        return;
    }

    const std::optional<QcItemId> item = itemForBlock(m_state.content(), *block);
    if (!item) {
        return;
    }

    // Already in the inventory: selected in the hotbar, or swapped in from the storage rows.
    for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
        if (client.inventory.slot(slot).item != *item || client.inventory.slot(slot).isEmpty()) {
            continue;
        }

        const int target = slot < QcInventory::hotbarSize ? slot : pickTarget(client);
        if (target != slot) {
            const QcItemStack moved = client.inventory.slot(slot);
            client.inventory.setSlot(slot, client.inventory.slot(target));
            client.inventory.setSlot(target, moved);
        }
        client.inventory.setSelectedHotbarSlot(target);
        m_inventory.send(client);
        return;
    }

    if (QcGameModes::rules(client.gameMode).itemCatalogue) {
        const int target = pickTarget(client);
        client.inventory.setSlot(target, {*item, m_state.content().item(*item).maxStack, 0});
        client.inventory.setSelectedHotbarSlot(target);
        m_inventory.send(client);
    }
}
