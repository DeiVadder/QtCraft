#pragma once

#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcItemStack.h"
#include "common/protocol/QcMessages.h"

#include <QElapsedTimer>
#include <QList>
#include <QString>

#include <functional>

class QcContentRegistry;
struct QcItemDefinition;

// The local player's items as the server last reported them (QcMsg::InventoryState): inventory,
// selected hotbar slot, worn equipment and the crafting grid with its result; and the requests that
// move items between these slots or craft (the server answers with a new InventoryState).
// The selected slot changes at once on the client (decision E-582): while selections the server
// has not answered yet are on their way, a state's selection is older than the local one and is
// not taken; the server answers each selection with a state carrying it, in order. A selection
// unanswered for pendingSelectionMs (dropped by the server's rate limit) is given up and the local
// selection is sent again, so client and server agree once more (checked with each state and tick).
class QcClientInventory
{
public:
    using Sender = std::function<void(const QcMessage &message)>;
    // Milliseconds on a steady clock (tests pass their own).
    using Clock = std::function<qint64()>;

    // `send`: how requests reach the server (the session's connection, never empty).
    // An unanswered selection is given up after this long, and the server's selection counts again.
    static constexpr qint64 pendingSelectionMs = 2000;

    explicit QcClientInventory(Sender send);
    // The default clock refers to this object.
    QcClientInventory(const QcClientInventory &) = delete;
    QcClientInventory &operator=(const QcClientInventory &) = delete;

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
    // Selections sent and not answered yet.
    qsizetype pendingSelections() const { return m_pendingSelections.size(); }
    // When the oldest selection has waited longer than pendingSelectionMs: gives the waiting ones
    // up and sends the local selection again (the session's tick, and each state).
    void resendLostSelection();
    void setClock(Clock clock) { m_now = std::move(clock); }
    // Slots 0..35 inventory, 36..44 crafting grid (see QcMsg::MoveInventoryItem); count 0 = all.
    void moveItem(int from, int to, int count = 0) const;
    // Shift-click: the server moves the stack to where it belongs (QcMsg::QuickMoveItem).
    void quickMoveItem(int slot) const;
    void takeCraftingResult() const;
    void craftRecipe(const QString &recipe, int count = 1) const;
    void closeCrafting() const;

private:
    struct PendingSelection
    {
        int slot = 0;
        qint64 sentMs = 0;
    };

    // Takes the server's selection unless a newer local one waits for its answer.
    void applySelection(int serverSlot);

    void sendSelection();

    Sender m_send;
    QElapsedTimer m_clock;
    Clock m_now;
    QList<PendingSelection> m_pendingSelections;
    QcInventory m_inventory;
    QcEquipment m_equipment;
    QList<QcItemStack> m_craftingGrid = QList<QcItemStack>(9);
    int m_craftingSize = 2;
    QcItemStack m_craftingResult;
};
