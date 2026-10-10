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
};

// Target slots in the order they are filled; empty when the stack cannot go anywhere.
QList<int> targets(int from, Situation situation);

} // namespace QcQuickMove
