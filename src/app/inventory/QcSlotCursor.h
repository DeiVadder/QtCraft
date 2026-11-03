// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/controllers/QcScreenState.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QcInventoryModel;

// What a slot click asks of the player's inventory (QcPlayerController): the requests the client
// sends to the server, which owns the inventory and decides. Rows are those of QcInventoryModel.
class QcSlotActions
{
public:
    virtual ~QcSlotActions() = default;

    // The row that takes no clicks (the hotbar slot of a backpack opened from the hand, decision
    // E-833), -1 for none.
    virtual int lockedRow() const = 0;
    // Moves `count` items from row `from` onto row `to`; count 0 moves the whole stack.
    virtual void moveItem(int from, int to, int count) const = 0;
    // Shift-click: the server moves the stack where it belongs (crafts all on the result).
    virtual void quickMove(int row) const = 0;
    virtual void takeCraftingResult() const = 0;
    // Creative mode: `count` items from the catalogue into a row (-1: into the inventory).
    virtual void putCreativeItem(int row, const QString &itemName, int count) const = 0;
    // Creative mode: empties a row (the bin).
    virtual void clearSlot(int row) const = 0;
};

// The mouse's hand on the slot screens (GameController.player.slotCursor, decision E-286): what a
// click on a slot, the creative catalogue or the bin does. Left click picks a stack up and puts it
// down elsewhere (merging or swapping); right click puts down one item and keeps the rest picked
// up; shift-click moves the whole stack where it belongs. The crafting result is taken (shift: as
// often as possible) instead of picked up; a machine's output can be picked up, nothing goes into
// it. In creative mode an item taken from the catalogue is carried instead: a click on a slot puts
// it there (right click: one item and it stays carried). Shown screens only forward their clicks;
// the screen state resets the hand whenever the overlay changes, and a picked-up stack that ran
// out is let go.
class QcSlotCursor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SlotCursor)
    QML_UNCREATABLE("Provided by GameController.player.slotCursor")
    // The row whose stack is picked up, -1 for none.
    Q_PROPERTY(int pickedSlot READ pickedSlot NOTIFY changed FINAL)
    // The catalogue item carried in creative mode (empty for none) and how many.
    Q_PROPERTY(QString carriedItem READ carriedItem NOTIFY changed FINAL)
    Q_PROPERTY(int carriedCount READ carriedCount NOTIFY changed FINAL)
    // Something is in the hand: a picked-up stack or a carried catalogue item.
    Q_PROPERTY(bool holding READ holding NOTIFY changed FINAL)

public:
    QcSlotCursor(const QcInventoryModel &inventory, const QcSlotActions &actions, QObject *parent = nullptr);

    int pickedSlot() const { return m_pickedSlot; }
    QString carriedItem() const { return m_carriedItem; }
    int carriedCount() const { return m_carriedCount; }
    bool holding() const { return pickedSlot() >= 0 || !carriedItem().isEmpty(); }
    // The output row of the shown screen's machine (nothing can be put into it), -1 for none.
    int outputRow() const { return m_outputRow; }
    // The machine's output row of a screen, -1 for none.
    static int outputRowOf(QcScreenState::Overlay overlay);

    // A click on an inventory model row: `button` left or right, `quick` for shift-click.
    Q_INVOKABLE void click(int row, Qt::MouseButton button, bool quick);
    // A click on an item of the creative catalogue: left takes a full stack (`maxStack`), right
    // one item, `quick` puts a full stack straight into the inventory. With a stack picked up, the
    // click deletes it instead.
    Q_INVOKABLE void takeFromCatalogue(const QString &itemName, int maxStack, Qt::MouseButton button, bool quick);
    // The creative bin: deletes the picked-up stack and lets go of a carried item.
    Q_INVOKABLE void bin();
    // Empties the hand: a click beside the slots puts the picked-up stack back (it never left its
    // slot) and drops the carried item.
    Q_INVOKABLE void reset();
    // Another screen shows (or none): the hand is emptied and takes that screen's output row.
    void showScreen(QcScreenState::Overlay overlay);
    // Picks up the stack of `row` without a click (test hook "pick:<row>").
    void pick(int row);

signals:
    void changed();

private:
    bool isEmpty(int row) const;
    void setPickedSlot(int row);
    void setCarried(const QString &itemName, int count);
    // The click with a catalogue item in the hand.
    void putCarried(int row, bool one);
    // The click on the crafting result.
    void takeResult(int row, bool quick) const;
    // The click with nothing in the hand.
    void pickUp(int row, bool quick);
    // The click with a stack picked up.
    void putPicked(int row, bool one);
    // The picked-up stack ran out (all put down one by one, or taken by someone else).
    void dropEmptyPick();

    const QcInventoryModel &m_inventory;
    const QcSlotActions &m_actions;
    int m_outputRow = -1;
    int m_pickedSlot = -1;
    QString m_carriedItem;
    int m_carriedCount = 0;
};
