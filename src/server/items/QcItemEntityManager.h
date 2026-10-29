// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "common/physics/QcEntityMotion.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcEntityComponent.h"
#include "server/entities/QcItemEntity.h"

#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <memory>
#include <utility>

class QcBlockAccess;

// Dropped item stacks lying in the world. They fall, slide to a stop, merge with equal stacks
// nearby, are picked up by players walking into them and despawn after a while. Only items in
// loaded chunks move and age; the others wait unchanged until their chunk loads again.
//
// Items below QcWorld::voidLimitY are removed, and drops there never appear (decision E-288).
//
// The number of items is bounded: above `activeLimit` awake items in loaded chunks new drops wait
// in a queue and appear as soon as older items are picked up or despawn. A chunk that loads with
// too many items keeps the surplus as surplus items (frozen, invisible) that wake up oldest first.
// The queue and the number of items (also those in unloaded chunks) are bounded as well, dropping
// the oldest when full.
class QcItemEntityManager
{
public:
    static constexpr double itemSize = 0.25;
    static constexpr double despawnSeconds = 300.0;
    // Seconds before a fresh drop can be picked up; items thrown by a player wait longer.
    static constexpr double pickupDelay = 0.5;
    static constexpr double thrownPickupDelay = 2.0;
    // Items touch a player when they are within the player's box grown by this much.
    static constexpr double pickupReach = 0.5;
    static constexpr double mergeRadius = 1.0;
    // Speed in m/s that items in flowing liquid drift with along its current (decision E-296).
    static constexpr double driftSpeed = 1.2;
    static constexpr int mergeIntervalTicks = 10;
    // The bounds by default (QcServerLimits::itemsAwake, itemsQueued, itemsTotal).
    static constexpr int activeLimit = static_cast<int>(QcServerLimits{}.itemsAwake);
    static constexpr int queueLimit = static_cast<int>(QcServerLimits{}.itemsQueued);
    static constexpr int totalLimit = static_cast<int>(QcServerLimits{}.itemsTotal);

    struct Drop
    {
        QcItemStack stack;
        QcVec3 position;
        QcVec3 velocity;
        double pickupDelay = QcItemEntityManager::pickupDelay;
        double age = 0.0;
        // What its saved entry holds that this version does not use (decision E-420).
        QcEntityKept kept = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

        bool operator==(const Drop &) const = default;
    };

    struct PlayerView
    {
        quint32 entityId = 0;
        QcVec3 position;
        bool alive = true;
    };

    // Called when an item touches a player; returns how many items did not fit into the inventory.
    using GiveFunction = std::function<int(quint32 playerEntityId, const QcItemStack &stack)>;
    using LoadedFunction = std::function<bool(const QcVec3 &position)>;

    QcItemEntityManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    // Adds a drop, or queues it when too many items are active. Returns the new id, 0 when queued,
    // empty or in the void.
    quint32 spawn(const Drop &drop, quint32 id);
    // Drops a stack at `position` with a small random upward velocity (mined blocks, loot).
    quint32 spawnScattered(const QcItemStack &stack, const QcVec3 &position, quint32 id);

    // What a tick asks the server.
    struct TickCallbacks
    {
        // Whether a waiting drop may appear at a position (its chunk is loaded).
        LoadedFunction isLoaded;
        GiveFunction give;
        std::function<quint32()> nextEntityId;
    };

    // One tick: physics, pickup, merging, ageing and despawning. Returns the items that despawned
    // of old age (the caller may plant saplings from them).
    QList<QcItemEntity> tick(const QcBlockAccess &world, const QList<PlayerView> &players, double dt, const TickCallbacks &callbacks);

    const QList<QcItemEntity> &items() const { return m_items; }
    // For moving items from outside their physics (pistons push them, decision E-337).
    QList<QcItemEntity> &editableItems() { return m_items; }
    const QList<Drop> &queued() const { return m_queue; }
    // Whether the queue is at least half full. Drops that matter little (blocks broken by an
    // explosion) stay away then, so the queue keeps room for what must not get lost (chest contents).
    bool crowded() const { return m_queue.size() * 2 >= m_queueLimit; }
    // Awake items in loaded chunks after the last tick (plus drops spawned since).
    int activeCount() const { return m_activeCount; }
    void setLimits(qsizetype active, qsizetype queued, qsizetype total);
    // Items removed because a limit was exceeded since the start.
    qint64 discardedCount() const { return m_discarded; }
    void clear();
    // Takes one item off the stack of item entity `id` (a seed planting itself); the entity goes when
    // its stack is empty. False when there is no such awake item.
    bool takeOne(quint32 id) { return take(id, 1); }
    // The same for `count` items (a hopper collecting a stack); false when it has fewer.
    bool take(quint32 id, int count);
    // Where items moved, appeared, grew by merging or woke from dormancy since the last call:
    // sleeping hoppers below them wake (decision E-344); resting items cost nothing.
    QList<QcVec3> takeStirred() { return std::exchange(m_stirred, {}); }
    // Removes the awake items that match (burning in lava); returns how many.
    qsizetype removeWhere(const std::function<bool(const QcItemEntity &)> &match);
    // Replaces all items, e.g. from a save; ids are assigned by `nextEntityId`.
    void restore(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId);
    // The same without clearing first: the drops of a chunk that came back (decision E-374).
    void adopt(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId);
    // Takes the waiting drops `match` picks out of the queue, in queue order.
    QList<Drop> takeQueued(const std::function<bool(const Drop &)> &match);

private:
    void step(QcItemEntity &item, const QcBlockAccess &world, double dt);
    void pickUp(QcItemEntity &item, const QList<PlayerView> &players, const GiveFunction &give);
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
