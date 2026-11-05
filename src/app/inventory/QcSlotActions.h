// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

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
