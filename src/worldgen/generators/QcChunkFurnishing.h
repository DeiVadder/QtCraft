// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QList>
#include <QString>

/*! \brief What a world generator puts into the block entities of a chunk it made (decision E-520):
    the loot of its chests, the creature kinds of its spawners (dungeons, mine shafts, the
    showcase's dungeon station) and its minecarts.

    The server puts it in once, when the chunk first arrives (QcFurnishing), and marks the chunk
    so that a chest emptied since is not filled again. */
struct QcChunkFurnishing
{
    /*! \brief A chest block to fill with loot. */
    struct Chest
    {
        /*! The position of the chest block. */
        QcBlockPos at;
        /*! Content name of the loot table (QcLootTables). */
        QString lootTable;
        /*! The seed of the loot draw and of how the loot is scattered over the slots. */
        quint64 seed = 0;
    };

    /*! \brief A spawner block and the creature kind it spawns. */
    struct Spawner
    {
        /*! The position of the spawner block. */
        QcBlockPos at;
        /*! Content name of the creature kind. */
        QString mob;
        /*! Its creatures are confined (QcCreature::confined; the showcase's spawner behind
            glass): they walk, but neither teleport nor change the world. */
        bool confined = false;
    };

    /*! \brief A minecart standing on the rail at \c {at} (mine shafts, decision E-570), placed
        from \c {item}; a container cart is filled from the loot table, if any. */
    struct Cart
    {
        /*! The position of the rail block. */
        QcBlockPos at;
        /*! Content name of the minecart item the cart is made from. */
        QString item;
        /*! Content name of the loot table for a container cart; empty or unknown for none. */
        QString lootTable;
        /*! The seed of the loot draw and of how the loot is scattered over the slots. */
        quint64 seed = 0;
    };

    /*! The chests to fill. */
    QList<Chest> chests;
    /*! The spawners to set up. */
    QList<Spawner> spawners;
    /*! The minecarts to place. */
    QList<Cart> carts;

    /*! Returns whether there are no chests, spawners and carts. */
    bool isEmpty() const { return chests.isEmpty() && spawners.isEmpty() && carts.isEmpty(); }
    /*! Appends the chests, spawners and carts of \a other (what another part of the generator
        furnishes the same chunk with). */
    void add(const QcChunkFurnishing &other)
    {
        chests.append(other.chests);
        spawners.append(other.spawners);
        carts.append(other.carts);
    }
};
