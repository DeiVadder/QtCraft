#pragma once

#include "common/core/QcBlockPos.h"

#include <QList>
#include <QString>

// What a world generator puts into the block entities of a chunk it made (decision E-520): the
// loot of its chests, the creature kinds of its spawners (dungeons, mine shafts, the showcase's
// dungeon station) and its minecarts. The server puts it in once, when the chunk first arrives
// (QcFurnishing), and marks the chunk so that a chest emptied since is not filled again.
struct QcChunkFurnishing
{
    struct Chest
    {
        QcBlockPos at;
        // Content name of the loot table (QcLootTables).
        QString lootTable;
        quint64 seed = 0;
    };

    struct Spawner
    {
        QcBlockPos at;
        QString mob;
        // Its creatures are confined (QcCreature::confined; the showcase's spawner behind glass):
        // they walk, but neither teleport nor change the world.
        bool confined = false;
    };

    // A minecart standing on the rail at `at` (mine shafts, decision E-570), placed from `item`; a
    // container cart is filled from the loot table, if any.
    struct Cart
    {
        QcBlockPos at;
        QString item;
        QString lootTable;
        quint64 seed = 0;
    };

    QList<Chest> chests;
    QList<Spawner> spawners;
    QList<Cart> carts;

    bool isEmpty() const { return chests.isEmpty() && spawners.isEmpty() && carts.isEmpty(); }
    // Takes in what another part of the generator furnishes the same chunk with.
    void add(const QcChunkFurnishing &other)
    {
        chests.append(other.chests);
        spawners.append(other.spawners);
        carts.append(other.carts);
    }
};
