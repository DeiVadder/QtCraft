#include "clientcore/player/QcClientInventory.h"

#include "common/content/registry/QcContentRegistry.h"

QcItemId QcClientInventory::heldItem() const
{
    const QcItemStack &held = m_inventory.selectedStack();
    return held.isEmpty() ? qcNoItem : held.item;
}

const QcItemDefinition *QcClientInventory::heldDefinition(const QcContentRegistry &content) const
{
    const QcItemStack &held = m_inventory.selectedStack();
    return !held.isEmpty() && content.isValidItem(held.item) ? &content.item(held.item) : nullptr;
}

bool QcClientInventory::apply(const QcMsg::InventoryState &state, const QcContentRegistry *content)
{
    if (state.stacks.size() != QcInventory::slotCount) {
        return false;
    }

    for (int i = 0; i < QcInventory::slotCount; ++i) {
        m_inventory.setSlot(i, state.stacks[i]);
    }
    m_inventory.setSelectedHotbarSlot(state.selected);

    if (state.craftingGrid.size() == 9 && (state.craftingSize == 2 || state.craftingSize == 3)) {
        m_craftingGrid = state.craftingGrid;
        m_craftingSize = state.craftingSize;
        m_craftingResult = state.craftingResult;
    }
    // Only items of the content are ever drawn (decision E-232).
    m_equipment = content ? state.equipment.knownTo(*content) : QcEquipment();
    return true;
}

void QcClientInventory::selectHotbarSlot(int slot)
{
    m_inventory.setSelectedHotbarSlot(slot);
    m_send(QcMsg::SelectHotbarSlot{m_inventory.selectedHotbarSlot()});
}

void QcClientInventory::moveItem(int from, int to, int count) const
{
    m_send(QcMsg::MoveInventoryItem{from, to, count});
}

void QcClientInventory::quickMoveItem(int slot) const
{
    m_send(QcMsg::QuickMoveItem{slot});
}

void QcClientInventory::takeCraftingResult() const
{
    m_send(QcMsg::TakeCraftingResult{});
}

void QcClientInventory::craftRecipe(const QString &recipe, int count) const
{
    m_send(QcMsg::CraftRecipe{recipe, count});
}

void QcClientInventory::closeCrafting() const
{
    m_send(QcMsg::CloseCrafting{});
}
