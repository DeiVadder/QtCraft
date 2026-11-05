// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <QHash>
#include <QList>

class QcBlockScreens;
class QcInventoryAccess;
class QcServerState;
struct QcMinecart;

/*! \brief How items move between minecarts and the blocks around them (decision E-387), once per
    tick after the carts moved, for awake carts only.

    The moves:

    \list
    \li a hopper cart that is enabled (no powered activator rail turned it off) and rested pulls one
        item from the container above the cell of its feet, or, without a container there, picks up
        the item entities lying in that cell or the one above (as much of a stack as fits);
    \li a Signal Hopper whose pickup cell (the cell above it, where a cart on a rail over it stands)
        holds a container cart pulls one item from it, if no power locks the hopper and its cooldown
        ended.
    \endlist
    Who acts and what moves follows the hoppers of decision E-344 (first slot that fits, the
    hoppers' cooldown after a move from QcServerLimits). Carts act in the order of their ids, so
    nothing about places or directions decides; at most QcServerLimits::hopperBudget carts act
    per tick, the next tick goes on after the last one. */
class QcMinecartHoppers
{
public:
    /*! \brief What the carts did in the latest tick. */
    struct Stats
    {
        /*! Container carts looked at and items moved in the latest tick. \qcshared */
        qsizetype acting = 0;
        qsizetype moved = 0;
    };

    QcMinecartHoppers(QcServerState &state, QcBlockScreens &screens);

    /*! Moves the items of one server tick. */
    void tick();
    /*! Returns what the carts did in the latest tick. */
    const Stats &lastTick() const { return m_stats; }
    /*! A tick without carts: nothing acted and nothing moved. */
    void forgetTick() { m_stats = {}; }

    /*! Returns the slot of \a stacks that takes one more of \a unit's item with stacks of at
        most \a maxStack, or -1 (first that fits). */
    static int slotFor(const QList<QcItemStack> &stacks, const QcItemStack &unit, int maxStack);

private:
    // The carts that act now, in the order of their ids after the last one of the previous tick.
    QList<qsizetype> actors();
    // What a hopper cart's pull from above did.
    enum class Pull
    {
        NoContainer, // nothing above to pull from (then it may pick up items)
        Nothing,     // a container, but nothing could move
        Moved,
    };

    // A hopper cart takes one item from the container above it.
    [[nodiscard]] Pull pullFromAbove(QcMinecart &cart, QcInventoryAccess &access);
    bool pickUp(QcMinecart &cart);
    // The hopper under the cart takes one item from it.
    bool unloadInto(QcMinecart &cart, QcBlockPos hopper, QcInventoryAccess &access);
    // Whether the Signal Hopper at `pos` may act now (loaded, unlocked, cooldown over).
    bool hopperReady(QcBlockPos pos) const;
    // Item entities by cell, for the cells hopper carts look at.
    void buildGrid();
    // The cart's slots changed: viewers see them, a comparator reading its detector rail looks again.
    void changed(const QcMinecart &cart);

    QcServerState &m_state;
    QcBlockScreens &m_screens;
    QHash<QcBlockPos, QList<qsizetype>> m_grid;
    bool m_gridBuilt = false;
    quint32 m_cursor = 0;
    Stats m_stats;
};
