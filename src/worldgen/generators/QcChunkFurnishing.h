#pragma once

#include "common/core/QcBlockPos.h"

#include <QList>
#include <QString>

// What a world generator puts into the block entities of a chunk it made (decision E-520): the
// loot of its chests and the creature kinds of its spawners (dungeons, the showcase's dungeon
// station). The server puts it in once, when the chunk first arrives (QcFurnishing), and marks the
// chunk so that a chest emptied since is not filled again.
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

    QList<Chest> chests;
    QList<Spawner> spawners;

    bool isEmpty() const { return chests.isEmpty() && spawners.isEmpty(); }
};
