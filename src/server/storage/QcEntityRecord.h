// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "server/entities/QcArmourStand.h"
#include "server/entities/QcBoat.h"
#include "server/entities/QcExperienceOrb.h"
#include "server/entities/QcFallingBlock.h"
#include "server/entities/QcHangingEntity.h"
#include "server/entities/QcMinecart.h"
#include "server/entities/QcPrimedExplosive.h"
#include "server/entities/QcProjectile.h"
#include "server/items/QcItemEntityManager.h"
#include "server/mobs/QcMobManager.h"

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
    static constexpr quint64 waitingOrderBase = quint64{1} << 32;

    QcChunkPos chunk;
    // Persistent creatures; the others are not saved and appear again around the players.
    QList<Ordered<QcMobManager::Stored>> creatures;
    // Items lying in the chunk and drops waiting to appear there.
    QList<Ordered<QcItemEntityManager::Drop>> items;
    // Falling blocks with their motion; the id is not saved.
    QList<Ordered<QcFallingBlock>> fallingBlocks;
    // Lit explosives with their motion and fuse, saved as falling blocks with a fuse (decision
    // E-601); the id is not saved.
    QList<Ordered<QcPrimedExplosive>> primedExplosives;
    // Armour stands; the id is not saved.
    QList<Ordered<QcArmourStand>> stands;
    // Minecarts with their motion; the id and the rider are not saved.
    QList<Ordered<QcMinecart>> minecarts;
    // Experience orbs with their motion, value and age; the id is not saved (decision E-398).
    QList<Ordered<QcExperienceOrb>> orbs;
    // Projectiles flying or stuck, by their type's name; the id and the shooter are not saved
    // (decision E-425).
    QList<Ordered<QcProjectile>> projectiles;
    // Item frames and paintings; the id is not saved (decision E-793).
    QList<Ordered<QcHangingEntity>> hangings;
    // Boats with their motion and item; the id and the riders are not saved (decision E-920).
    QList<Ordered<QcBoat>> boats;
    // The chunk got its group of animals already (decision E-086); kept with the chunk.
    bool populated = false;

    // No entities; a populated chunk still keeps its record.
    bool isEmpty() const
    {
        return creatures.isEmpty() && items.isEmpty() && fallingBlocks.isEmpty() && stands.isEmpty() && minecarts.isEmpty()
               && orbs.isEmpty() && projectiles.isEmpty() && hangings.isEmpty()
               && boats.isEmpty();
    }
};


