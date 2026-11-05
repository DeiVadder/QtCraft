// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "common/physics/QcEntityMotion.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcEntityComponent.h"
#include "server/entities/QcItemEntity.h"
#include "server/items/QcPickupPlayer.h"

#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <memory>
#include <utility>

class QcBlockAccess;

/*! \brief Dropped item stacks lying in the world.

    They fall, slide to a stop, merge with equal stacks nearby, are picked up by players walking
    into them and despawn after a while. Only items in loaded chunks move and age; the others wait
    unchanged until their chunk loads again.

    Items below QcWorld::voidLimitY are removed, and drops there never appear (decision E-288).

    The number of items is bounded: above \c {activeLimit} awake items in loaded chunks new drops
    wait in a queue and appear as soon as older items are picked up or despawn. A chunk that
    loads with too many items keeps the surplus as surplus items (frozen, invisible) that wake up
    oldest first. The queue and the number of items (also those in unloaded chunks) are bounded
    as well, dropping the oldest when full. */
class QcItemEntityManager
{
public:
    /*! Width and height of an item's box in blocks. */
    static constexpr double itemSize = 0.25;
    /*! Seconds an item in a loaded chunk lasts. */
    static constexpr double despawnSeconds = 300.0;
    /*! Seconds before a fresh drop can be picked up; items thrown by a player wait longer.
        \qcshared */
    static constexpr double pickupDelay = 0.5;
    static constexpr double thrownPickupDelay = 2.0;
    /*! Items touch a player when they are within the player's box grown by this much. */
    static constexpr double pickupReach = 0.5;
    /*! Equal stacks closer than this many blocks merge, every mergeIntervalTicks ticks. */
    static constexpr double mergeRadius = 1.0;
    /*! Speed in m/s that items in flowing liquid drift with along its current (decision E-296). */
    static constexpr double driftSpeed = 1.2;
    /*! Ticks between two merges. */
    static constexpr int mergeIntervalTicks = 10;
    /*! The bounds by default (QcServerLimits::itemsAwake, itemsQueued, itemsTotal). \qcshared */
    static constexpr int activeLimit = static_cast<int>(QcServerLimits{}.itemsAwake);
    static constexpr int queueLimit = static_cast<int>(QcServerLimits{}.itemsQueued);
    static constexpr int totalLimit = static_cast<int>(QcServerLimits{}.itemsTotal);

    /*! \brief An item stack that is to appear or waits in the queue: the stack, its position
        (feet) and velocity, the seconds before it can be picked up and its age in seconds.
        \qcfields */
    struct Drop
    {
        QcItemStack stack;
        QcVec3 position;
        QcVec3 velocity;
        double pickupDelay = QcItemEntityManager::pickupDelay;
        double age = 0.0;
        /*! What its saved entry holds that this version does not use (decision E-420). */
        QcEntityKept kept = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

        bool operator==(const Drop &) const = default;
    };

    /*! Called when an item touches the player \c {playerEntityId} with its \c {stack}; returns
        how many items did not fit into the inventory. */
    using GiveFunction = std::function<int(quint32 playerEntityId, const QcItemStack &stack)>;
    /*! Returns whether the chunk at \c {position} is loaded. */
    using LoadedFunction = std::function<bool(const QcVec3 &position)>;

    QcItemEntityManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    /*! Adds \a drop as the item \a id, or queues it when too many items are active (more than a
        stack: the rest is queued). Returns \a id, 0 when queued, empty, unknown or in the void. */
    quint32 spawn(const Drop &drop, quint32 id);
    /*! Drops \a stack at \a position as the item \a id with a small random upward velocity
        (mined blocks, loot). Returns what spawn() returns. */
    quint32 spawnScattered(const QcItemStack &stack, const QcVec3 &position, quint32 id);

    /*! \brief What a tick asks the server.

        Besides isLoaded: give hands a touched item to a player, nextEntityId gives the id of a
        waiting drop that appears. \qcfields */
    struct TickCallbacks
    {
        /*! Whether a waiting drop may appear at a position (its chunk is loaded). */
        LoadedFunction isLoaded;
        GiveFunction give;
        std::function<quint32()> nextEntityId;
    };

    /*! One tick of \a dt seconds in \a world: physics, pickup by \a players, merging, ageing and
        despawning, through \a callbacks. Returns the items that despawned of old age (the caller
        may plant saplings from them). */
    QList<QcItemEntity> tick(const QcBlockAccess &world, const QList<QcPickupPlayer> &players, double dt, const TickCallbacks &callbacks);

    /*! Returns the awake items. */
    const QList<QcItemEntity> &items() const { return m_items; }
    /*! Returns the items for moving them from outside their physics (pistons push them, decision
        E-337). */
    QList<QcItemEntity> &editableItems() { return m_items; }
    /*! Returns the drops waiting to appear, oldest first. */
    const QList<Drop> &queued() const { return m_queue; }
    /*! Returns whether the queue is at least half full. Drops that matter little (blocks broken
        by an explosion) stay away then, so the queue keeps room for what must not get lost
        (chest contents). */
    bool crowded() const { return m_queue.size() * 2 >= m_queueLimit; }
    /*! Returns the awake items in loaded chunks after the last tick (plus drops spawned since). */
    int activeCount() const { return m_activeCount; }
    /*! Sets the bounds: \a active awake items, \a queued waiting drops, \a total items. */
    void setLimits(qsizetype active, qsizetype queued, qsizetype total);
    /*! Returns the items removed because a limit was exceeded since the start. */
    qint64 discardedCount() const { return m_discarded; }
    /*! Removes every item and every waiting drop. */
    void clear();
    /*! Takes one item off the stack of item entity \a id (a seed planting itself); the entity
        goes when its stack is empty. Returns \c false when there is no such awake item. */
    bool takeOne(quint32 id) { return take(id, 1); }
    /*! Takes \a count items off the stack of item entity \a id (a hopper collecting a stack).
        Returns \c false when there is no such awake item or it has fewer. */
    bool take(quint32 id, int count);
    /*! Returns where items moved, appeared, grew by merging or woke from dormancy since the last
        call: sleeping hoppers below them wake (decision E-344); resting items cost nothing. */
    QList<QcVec3> takeStirred() { return std::exchange(m_stirred, {}); }
    /*! Removes the awake items \a match picks (burning in lava). Returns how many. */
    qsizetype removeWhere(const std::function<bool(const QcItemEntity &)> &match);
    /*! Replaces all items with \a drops, e.g. from a save; ids are assigned by \a nextEntityId. */
    void restore(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId);
    /*! Adds \a drops with ids from \a nextEntityId without clearing first: the drops of a chunk
        that came back (decision E-374). */
    void adopt(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId);
    /*! Takes the waiting drops \a match picks out of the queue. Returns them in queue order. */
    QList<Drop> takeQueued(const std::function<bool(const Drop &)> &match);

private:
    void step(QcItemEntity &item, const QcBlockAccess &world, double dt);
    void pickUp(QcItemEntity &item, const QList<QcPickupPlayer> &players, const GiveFunction &give);
    // Drops emptied items and items in the void, collects the ones that despawned of old age and recounts the awake ones.
    QList<QcItemEntity> removeFinished(const QList<bool> &awake);
    void spawnQueued(const TickCallbacks &callbacks);
    void merge(const QList<bool> &active);
    void enqueue(const Drop &drop);
    void enforceTotalLimit();
    void balanceDormancy(const QList<bool> &loaded);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator m_random;
    QList<QcItemEntity> m_items;
    QList<Drop> m_queue;
    QList<QcVec3> m_stirred;
    int m_activeCount = 0;
    qsizetype m_activeLimit = activeLimit;
    qsizetype m_queueLimit = queueLimit;
    qsizetype m_totalLimit = totalLimit;
    quint64 m_ticks = 0;
    qint64 m_discarded = 0;
    // Waiting drops discarded since the last warning; one warning per tick says how many.
    qint64 m_discardedUnreported = 0;
};
