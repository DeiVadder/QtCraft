#pragma once

#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcItemStack.h"
#include "common/protocol/QcMessages.h"

#include <QList>
#include <QString>

#include <functional>

class QcContentRegistry;
struct QcItemDefinition;

// The local player's items as the server last reported them (QcMsg::InventoryState): inventory,
// selected hotbar slot, worn equipment and the crafting grid with its result; and the requests that
// move items between these slots or craft (the server answers with a new InventoryState).
class QcClientInventory
{
public:
    using Sender = std::function<void(const QcMessage &message)>;

    // `send`: how requests reach the server (the session's connection, never empty).
    explicit QcClientInventory(Sender send)
        : m_send(std::move(send))
    {
    }

    const QcInventory &inventory() const { return m_inventory; }
    const QcEquipment &equipment() const { return m_equipment; }
    const QList<QcItemStack> &craftingGrid() const { return m_craftingGrid; }
    int craftingSize() const { return m_craftingSize; }
    QcItemStack craftingResult() const { return m_craftingResult; }
    // The item in the selected hotbar slot: its id (qcNoItem for an empty hand) and its definition
    // (null for an empty hand or an item unknown to `content`).
    QcItemId heldItem() const;
    const QcItemDefinition *heldDefinition(const QcContentRegistry &content) const;

    // Takes the server's state (`content`: the session's, null before it arrived); false without a
    // change when the state has the wrong number of slots.
    bool apply(const QcMsg::InventoryState &state, const QcContentRegistry *content);

    // Selects a hotbar slot at once and tells the server.
    void selectHotbarSlot(int slot);
    // Slots 0..35 inventory, 36..44 crafting grid (see QcMsg::MoveInventoryItem); count 0 = all.
    void moveItem(int from, int to, int count) const;
    // Shift-click: the server moves the stack to where it belongs (QcMsg::QuickMoveItem).
    void quickMoveItem(int slot) const;
    void takeCraftingResult() const;
    void craftRecipe(const QString &recipe, int count) const;
    void closeCrafting() const;

private:
    Sender m_send;
    QcInventory m_inventory;
    QcEquipment m_equipment;
    QList<QcItemStack> m_craftingGrid = QList<QcItemStack>(9);
    int m_craftingSize = 2;
    QcItemStack m_craftingResult;
};
