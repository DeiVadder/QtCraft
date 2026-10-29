// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

// Where a shift-clicked stack goes, by its slot (numbers as in QcMsg::MoveInventoryItem). Pure, so
// the server's rule is testable on its own.
namespace QcQuickMove {

struct Situation
{
    // Slots of the open chest (0: none).
    int containerSlots = 0;
    // A machine (furnace) is open, and whether the moved item can be smelted there or burnt.
    bool machineOpen = false;
    bool smeltable = false;
    bool fuel = false;
    // The moved item is armour whose slot is empty: the slot number (MoveInventoryItem::equipmentSlot
    // onwards), else -1. With no block open, armour from the inventory goes there first.
    int freeArmourSlot = -1;
    // A backpack opened from the hand (decision E-833): its slots go into the hotbar only.
    bool hotbarOnly = false;
};

// Target slots in the order they are filled; empty when the stack cannot go anywhere.
QList<int> targets(int from, Situation situation);

} // namespace QcQuickMove
