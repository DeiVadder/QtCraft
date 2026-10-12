#include "QcServerGameModes.h"

#include "QcArmour.h"
#include "QcServerInventory.h"
#include "QcServerState.h"

#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(lcGameModes, "qtcraft.gamemodes")

} // namespace

QcServerGameModes::QcServerGameModes(QcServerState &state, QcServerInventory &inventory)
    : m_state(state)
    , m_inventory(inventory)
{
}

QcGameMode QcServerGameModes::defaultMode() const
{
    return m_state.config.defaultGameMode.value_or(m_state.info.defaultGameMode);
}

void QcServerGameModes::setMode(QcServerClient &client, QcGameMode mode)
{
    if (QcGameModes::inventorySet(client.gameMode) != QcGameModes::inventorySet(mode)) {
        // The grid's items belong to the inventory that is put aside.
        m_inventory.returnCraftingGrid(client);
        std::swap(client.inventory, client.keptInventory);
        const QcEquipment worn = client.keptEquipment;
        client.keptEquipment = client.equipment;
        m_inventory.setEquipment(client, worn);
    }

    client.gameMode = mode;
    applyRules(client);
    // The new rules may end a flight; the client hears it with the next state.
    client.stateDirty = true;
    client.miningPos.reset();
    send(client);
    m_inventory.send(client);
}

void QcServerGameModes::applyRules(QcServerClient &client)
{
    const QcGameModes::Rules rules = QcGameModes::rules(client.gameMode);
    client.hunger.setFrozen(rules.hungerFrozen);
    if (!rules.mayFly) {
        client.motion.flying = false;
    }
}

void QcServerGameModes::send(QcServerClient &client)
{
    client.connection->send(QcMsg::GameMode{client.gameMode});
}

QcItemStack QcServerGameModes::validStack(QcItemStack stack) const
{
    const QcContentRegistry &content = *m_state.content;
    if (stack.count <= 0 || !content.isValidItem(stack.item)) {
        return {};
    }

    const QcItemDefinition &definition = content.item(stack.item);
    return {stack.item, std::clamp(stack.count, 1, definition.maxStack), std::clamp(stack.damage, 0, std::max(0, definition.durability - 1))};
}

void QcServerGameModes::creativeItem(QcServerClient &client, QcMsg::CreativeItem message)
{
    if (!QcGameModes::rules(client.gameMode).itemCatalogue || client.dead) {
        // A client out of step with its mode (or a cheat): it gets the true inventory back.
        qCDebug(lcGameModes) << "Refused a creative item for" << client.name << "in" << QcGameModes::name(client.gameMode);
        m_inventory.send(client);
        return;
    }

    const QcItemStack stack = validStack(message.stack);
    if (message.slot == QcMsg::CreativeItem::addSlot) {
        if (!stack.isEmpty()) {
            client.inventory.add(stack.item, stack.count, *m_state.content, stack.damage);
        }
    } else if (QcMsg::MoveInventoryItem::isEquipmentSlot(message.slot)) {
        putIntoArmourSlot(client, message.slot - QcMsg::MoveInventoryItem::equipmentSlot, stack);
    } else if (QcInventory::isValidSlot(message.slot)) {
        putIntoSlot(client, message.slot, stack);
    }
    m_inventory.send(client);
}

void QcServerGameModes::putIntoSlot(QcServerClient &client, int slot, QcItemStack stack) const
{
    const QcItemStack existing = client.inventory.slot(slot);
    if (!stack.isEmpty() && !existing.isEmpty() && existing.item == stack.item && existing.damage == stack.damage) {
        const int limit = m_state.content->item(stack.item).maxStack;
        client.inventory.setSlot(slot, {stack.item, std::min(existing.count + stack.count, limit), stack.damage});
        return;
    }
    client.inventory.setSlot(slot, stack);
}

bool QcServerGameModes::putIntoArmourSlot(QcServerClient &client, int slot, QcItemStack stack) const
{
    if (!stack.isEmpty() && !QcArmour::fits(*m_state.content, stack, QcEquipmentSlot(slot))) {
        return false;
    }

    QcEquipment worn = client.equipment;
    worn.stacks[std::size_t(slot)] = stack.isEmpty() ? QcItemStack{} : QcItemStack{stack.item, 1, stack.damage};
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
    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    // Like mining and placing: within reach and in sight, not through walls.
    if (client.dead || !block || *block == qcAirBlockId || !m_state.isWithinReach(client, pos) || !m_state.canSee(client, pos)) {
        return;
    }

    const std::optional<QcItemId> item = itemForBlock(*m_state.content, *block);
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
        client.inventory.setSlot(target, {*item, m_state.content->item(*item).maxStack, 0});
        client.inventory.setSelectedHotbarSlot(target);
        m_inventory.send(client);
    }
}
