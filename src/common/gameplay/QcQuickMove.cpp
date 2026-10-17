#include "common/gameplay/QcQuickMove.h"

#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcMessages.h"

namespace QcQuickMove {

namespace {

constexpr int gridSlot = QcMsg::MoveInventoryItem::craftingGridSlot;
constexpr int blockSlot = QcMsg::MoveInventoryItem::machineInputSlot;

QList<int> range(int first, int count)
{
    QList<int> result;
    for (int slot = first; slot < first + count; ++slot) {
        result.append(slot);
    }
    return result;
}

// Hotbar first, then the storage rows, like picking items up.
QList<int> wholeInventory()
{
    return range(0, QcInventory::slotCount);
}

QList<int> intoOpenBlock(Situation situation)
{
    if (situation.containerSlots > 0) {
        return range(blockSlot, situation.containerSlots);
    }
    // Into the machine: what it can smelt goes into the input, fuel onto the fuel slot.
    QList<int> result;
    if (situation.smeltable) {
        result.append(QcMsg::MoveInventoryItem::machineInputSlot);
    }
    if (situation.fuel) {
        result.append(QcMsg::MoveInventoryItem::machineFuelSlot);
    }
    return result;
}

} // namespace

QList<int> targets(int from, Situation situation)
{
    if (from >= gridSlot) {
        // From the crafting grid, the open block or an armour slot into the inventory.
        return wholeInventory();
    }
    if (from < 0 || from >= QcInventory::slotCount) {
        return {};
    }
    if (situation.containerSlots > 0 || situation.machineOpen) {
        return intoOpenBlock(situation);
    }
    if (situation.freeArmourSlot >= 0) {
        return {situation.freeArmourSlot};
    }
    // Between the hotbar and the storage rows.
    return from < QcInventory::hotbarSize ? range(QcInventory::hotbarSize, QcInventory::slotCount - QcInventory::hotbarSize)
                                          : range(0, QcInventory::hotbarSize);
}

} // namespace QcQuickMove
