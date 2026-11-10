// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/core/QcBlockPos.h"
#include "worldgen/terrain/QcNoise.h"

#include <QList>
#include <QString>

/*! \brief What a world generator puts into the block entities of a chunk it made (decision E-520):
    the loot of its chests, the creature kinds of its spawners (dungeons, mine shafts, the
    showcase's dungeon station), its minecarts and those who stand in it from the start (bastions).

    The server puts it in once, when the chunk first arrives (QcFurnishing), and marks the chunk
    so that a chest emptied since is not filled again. */
struct QcChunkFurnishing
{
    /*! Returns the seed of the loot of the chest at \a at of a structure with \a lootSeed. */
    [[nodiscard]] static quint64 chestSeed(quint64 lootSeed, QcBlockPos at)
    {
        return qcMixSeed(lootSeed, static_cast<quint64>(qcPositionRandom(lootSeed, at.x, at.y, at.z) * 4294967296.0));
    }

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

    /*! \brief A creature that stands in a structure from the start, kept with the world (decision
        E-1089). \qcfields */
    struct Resident
    {
        QcBlockPos at;
        QString mob;
    };

    /*! The chests to fill. */
    QList<Chest> chests;
    /*! The spawners to set up. */
    QList<QcStructureDefinition::Spawner> spawners;
    /*! The minecarts to place. */
    QList<Cart> carts;
    /*! Those who stand there from the start. */
    QList<Resident> residents;

    /*! Returns whether there are no chests, spawners, carts and residents. */
    bool isEmpty() const { return chests.isEmpty() && spawners.isEmpty() && carts.isEmpty() && residents.isEmpty(); }
    /*! Appends the chests, spawners, carts and residents of \a other (what another part of the
        generator furnishes the same chunk with). */
    void add(const QcChunkFurnishing &other)
    {
        chests.append(other.chests);
        spawners.append(other.spawners);
        carts.append(other.carts);
        residents.append(other.residents);
    }
};
