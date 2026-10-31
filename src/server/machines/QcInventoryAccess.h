// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QSet>

class QcServerState;

/*! \brief The slots of containers (chests, barrels, hoppers, dispensers) and machines (furnaces) as
    Signal Hoppers, droppers and dispensers reach them (decision E-344).

    A furnace takes items from above into its input and from the other sides into its fuel slot
    (fuel only) and gives its output downwards. Items go into the first slot in index order that is
    empty or holds the same item with room, as in the reference.

    Changes are units added to and taken from slots; QcServerState::noteContents tells
    comparators and hoppers around once finish() runs. */
class QcInventoryAccess
{
public:
    /*! \brief One unit (or the whole stack of an item entity) going from a source into a target
        slot. */
    struct Insertion
    {
        /*! The position of the target container or machine. */
        QcBlockPos target;
        /*! The face of the target the item enters through (Up: from above). */
        QcBlockFace face = QcBlockFace::Up;
        /*! The items that go. */
        QcItemStack stack;
        /*! Who gives it: an index the caller understands (a hopper, a dispenser). */
        int giver = -1;
        /*! Filled by resolve(): the slot it goes into. */
        int slot = -1;
    };

    explicit QcInventoryAccess(QcServerState &state);

    /*! Returns whether the block at \a pos is a container or machine. */
    bool hasInventory(QcBlockPos pos) const;
    /*! Makes sure the container or machine at \a pos has its entry (it has none when the table
        was full as the block was placed), so items can go into it. Returns \c false when it has no
        inventory or the table of entries is full. Called while deciding, so a step never moves
        an item into nowhere. */
    bool ensure(QcBlockPos pos);
    /*! Returns a copy of the slots at \a pos (an unopened container is empty); empty without an
        inventory. */
    QList<QcItemStack> slotsAt(QcBlockPos pos) const;
    /*! Returns the slots of the inventory at \a pos an item may enter through \a face. */
    QList<int> slotsIn(QcBlockPos pos, QcBlockFace face) const;
    /*! Returns the slots of the inventory at \a pos an item may leave through \a face. */
    QList<int> slotsOut(QcBlockPos pos, QcBlockFace face) const;
    /*! Returns whether \a stack may leave \a slot of the inventory at \a pos (a machine's fuel
        slot gives only an empty bucket back). */
    bool mayTakeOut(QcBlockPos pos, int slot, const QcItemStack &stack) const;
    /*! Returns the first slot of \a cells in \a stacks of the inventory at \a pos that takes one
        more of \a stack's item, or -1. */
    int slotFor(QcBlockPos pos, const QList<QcItemStack> &stacks, const QList<int> &cells, const QcItemStack &stack) const;
    /*! Returns where as much of \a stack as fits into \a stacks of the inventory at \a pos
        through \a cells goes: slot and count each. */
    QList<std::pair<int, int>> fit(QcBlockPos pos, QList<QcItemStack> stacks, const QList<int> &cells, const QcItemStack &stack) const;

    /*! Decides which \a insertions into the same targets happen together (one unit each): per
        target the one from above first, then the others grouped by item in a fixed item order,
        each group whole or not at all, so no direction or coordinate decides (decision E-344).
        Returns the kept ones with their slots. */
    QList<Insertion> resolve(QList<Insertion> insertions) const;

    /*! Takes \a count from \a slot of the inventory at \a pos (the slot was chosen by this
        class). Returns \c false when the slot does not exist. Callers put first and take only
        after a successful put. */
    [[nodiscard]] bool take(QcBlockPos pos, int slot, int count);
    /*! Puts \a stack into \a slot of the inventory at \a pos (chosen by this class). Returns
        \c false when the slot does not exist. */
    [[nodiscard]] bool put(QcBlockPos pos, int slot, const QcItemStack &stack);
    /*! Empties slots left at count 0 and tells the neighbours about every changed inventory. */
    void finish();
    /*! Returns whether the hopper at \a pos received something while it was empty since the last
        finish(). */
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
