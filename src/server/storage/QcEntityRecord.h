#pragma once

#include "QcArmourStand.h"
#include "QcChunkPos.h"
#include "QcFallingBlock.h"
#include "QcItemEntityManager.h"
#include "QcMobManager.h"

#include <QList>
#include <QSet>

// What is saved of the entities of one chunk (decision E-366): the chunk holding each one's feet.
// Every entry carries a key of its place in the table of its kind (`order`): its entity id (tables
// only append and keep their order when they remove, so id order is table order), for drops waiting
// to appear waitingOrderBase plus their place in the queue. The key does not change while an entity
// lives, so a record changes only when its own chunk's entities do; sorted by it, the tables come
// back in the same order and the world goes on exactly as it was saved.
struct QcEntityRecord
{
    template<typename Entry>
    struct Ordered
    {
        Entry entry;
        quint64 order = 0;
    };

    // Waiting drops come after every item that already lies in the world.
    static constexpr quint64 waitingOrderBase = quint64(1) << 32;

    QcChunkPos chunk;
    // Persistent creatures; the others are not saved and appear again around the players.
    QList<Ordered<QcMobManager::Stored>> creatures;
    // Items lying in the chunk and drops waiting to appear there.
    QList<Ordered<QcItemEntityManager::Drop>> items;
    // Falling blocks with their motion; the id is not saved.
    QList<Ordered<QcFallingBlock>> fallingBlocks;
    // Armour stands; the id is not saved.
    QList<Ordered<QcArmourStand>> stands;

    bool isEmpty() const { return creatures.isEmpty() && items.isEmpty() && fallingBlocks.isEmpty() && stands.isEmpty(); }
};

// The saved entities of a world: a record per chunk with any, and the chunks that already got their
// group of animals (decision E-086).
struct QcWorldEntityRecords
{
    QList<QcEntityRecord> records;
    QSet<QcChunkPos> populated;
};
