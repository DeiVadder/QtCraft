// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/*! \brief The local player's items as the server last reported them (QcMsg::InventoryState):
    inventory, selected hotbar slot, worn equipment and the crafting grid with its result; and the
    requests that move items between these slots or craft (the server answers with a new
    InventoryState).

    The selected slot changes at once on the client (decision E-582): while selections the server
    has not answered yet are on their way, a state's selection is older than the local one and is
    not taken; the server answers each selection with a state carrying it, in order. A selection
    unanswered for pendingSelectionMs (dropped by the server's rate limit) is given up and the
    local selection is sent again, so client and server agree once more (checked with each state
    and tick). */
class QcClientInventory
{
public:
    /*! Sends a request to the server. */
    using Sender = std::function<void(const QcMessage &message)>;
    /*! Milliseconds on a steady clock (tests pass their own). */
    using Clock = std::function<qint64()>;

    /*! An unanswered selection is given up after this long, and the server's selection counts
        again. */
    static constexpr qint64 pendingSelectionMs = 2000;

    /*! \a send: how requests reach the server (the session's connection, never empty). */
    explicit QcClientInventory(Sender send);
    /*! The default clock refers to this object. */
    QcClientInventory(const QcClientInventory &) = delete;
    QcClientInventory &operator=(const QcClientInventory &) = delete;
    ~QcClientInventory() = default;

    /*! Returns the inventory as the server last reported it. */
    const QcInventory &inventory() const { return m_inventory; }
    /*! Returns the worn equipment. */
    const QcEquipment &equipment() const { return m_equipment; }
    /*! Returns the slots of the crafting grid (9; the inventory's grid uses 2 x 2 of them). */
    const QList<QcItemStack> &craftingGrid() const { return m_craftingGrid; }
    /*! Returns the edge length of the open crafting grid: 2 in the inventory, 3 at a crafting
        table. */
    int craftingSize() const { return m_craftingSize; }
    /*! Returns the result of the crafting grid as the server reported it. */
    QcItemStack craftingResult() const { return m_craftingResult; }
    /*! Returns the item in the selected hotbar slot: its id (qcNoItem for an empty hand) and its
        definition (null for an empty hand or an item unknown to \c {content}). */
    QcItemId heldItem() const;
    /*! Returns the definition of the item in the selected hotbar slot in \a content; null for an
        empty hand or an unknown item. */
    const QcItemDefinition *heldDefinition(const QcContentRegistry &content) const;

    /*! Takes the server's \a state (\a content: the session's, null before it arrived). Returns
        false without a change when the state has the wrong number of slots. */
    bool apply(const QcMsg::InventoryState &state, const QcContentRegistry *content);

    /*! Selects the hotbar \a slot at once and tells the server. */
    void selectHotbarSlot(int slot);
    /*! Returns the number of selections sent and not answered yet. */
    qsizetype pendingSelections() const { return m_pendingSelections.size(); }
    /*! When the oldest selection has waited longer than pendingSelectionMs: gives the waiting
        ones up and sends the local selection again (the session's tick, and each state). */
    void resendLostSelection();
    /*! Replaces the clock of the selections with \a clock (tests). */
    void setClock(Clock clock) { m_now = std::move(clock); }
    /*! Asks the server to move \a count items (0: all) from slot \a from to slot \a to: slots 0..35
        inventory, 36..44 crafting grid (see QcMsg::MoveInventoryItem). */
    void moveItem(int from, int to, int count = 0) const;
    /*! Shift-click on \a slot: the server moves the stack to where it belongs
        (QcMsg::QuickMoveItem). */
    void quickMoveItem(int slot) const;
    /*! Asks the server to move the crafting result to the inventory. */
    void takeCraftingResult() const;
    /*! Asks the server to craft the recipe named \a recipe \a count times from the inventory. */
    void craftRecipe(const QString &recipe, int count = 1) const;
    /*! Asks the server to close the crafting grid; its items go back. */
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
