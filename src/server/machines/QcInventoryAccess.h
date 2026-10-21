#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QSet>

class QcServerState;

// The slots of containers (chests, barrels, hoppers, dispensers) and machines (furnaces) as Signal
// Hoppers, droppers and dispensers reach them (decision E-344). A furnace takes items from above into
// its input and from the other sides into its fuel slot (fuel only) and gives its output downwards.
// Items go into the first slot in index order that is empty or holds the same item with room, as in
// the reference.
//
// Changes are units added to and taken from slots; QcServerState::noteContents tells comparators and
// hoppers around once `finish()` runs.
class QcInventoryAccess
{
public:
    // One unit (or the whole stack of an item entity) going from a source into a target slot.
    struct Insertion
    {
        QcBlockPos target;
        // The face of the target the item enters through (Up: from above).
        QcBlockFace face = QcBlockFace::Up;
        QcItemStack stack;
        // Who gives it: an index the caller understands (a hopper, a dispenser).
        int giver = -1;
        // Filled by resolve(): the slot it goes into.
        int slot = -1;
    };

    explicit QcInventoryAccess(QcServerState &state);

    bool hasInventory(QcBlockPos pos) const;
    // Makes sure the container or machine at `pos` has its entry (it has none when the table was
    // full as the block was placed), so items can go into it; false when it has no inventory or the
    // table of entries is full. Called while deciding, so a step never moves an item into nowhere.
    bool ensure(QcBlockPos pos);
    // A copy of the slots (an unopened container is empty); empty without an inventory.
    QList<QcItemStack> slotsAt(QcBlockPos pos) const;
    // Slots an item may enter through `face` and leave through `face`.
    QList<int> slotsIn(QcBlockPos pos, QcBlockFace face) const;
    QList<int> slotsOut(QcBlockPos pos, QcBlockFace face) const;
    // Whether `stack` may leave that slot (a machine's fuel slot gives only an empty bucket back).
    bool mayTakeOut(QcBlockPos pos, int slot, const QcItemStack &stack) const;
    // The first slot of `slots` in `stacks` that takes one more of `stack`'s item, or -1.
    int slotFor(QcBlockPos pos, const QList<QcItemStack> &stacks, const QList<int> &cells, const QcItemStack &stack) const;
    // Where as much of `stack` as fits into `stacks` through `slots` goes: slot and count each.
    QList<std::pair<int, int>> fit(QcBlockPos pos, QList<QcItemStack> stacks, const QList<int> &cells, const QcItemStack &stack) const;

    // Decides which insertions into the same targets happen together (one unit each): per target the
    // one from above first, then the others grouped by item in a fixed item order, each group whole or
    // not at all, so no direction or coordinate decides (decision E-344). Returns the kept ones with
    // their slots.
    QList<Insertion> resolve(QList<Insertion> insertions) const;

    // Takes `count` from a slot / puts a stack into a slot (the slot was chosen by this class); false
    // when the slot does not exist. Callers put first and take only after a successful put.
    [[nodiscard]] bool take(QcBlockPos pos, int slot, int count);
    [[nodiscard]] bool put(QcBlockPos pos, int slot, const QcItemStack &stack);
    // Empties slots left at count 0 and tells the neighbours about every changed inventory.
    void finish();
    // Whether the hopper at `pos` received something while it was empty since the last finish().
    bool wasEmptyBefore(QcBlockPos pos) const { return m_filledFromEmpty.contains(pos); }

private:
    bool accepts(QcBlockPos pos, int slot, const QcItemStack &stack) const;
    QcItemStack *slotPtr(QcBlockPos pos, int slot);
    // Insertions into one target, kept in place.
    QList<Insertion> resolveTarget(QList<Insertion> group) const;

    QcServerState &m_state;
    QSet<QcBlockPos> m_changed;
    QSet<QcBlockPos> m_filledFromEmpty;
};
