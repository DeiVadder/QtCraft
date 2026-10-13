#pragma once

#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcItemStack.h"

#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <memory>

class QcBlockAccess;

// Dropped item stacks lying in the world. They fall, slide to a stop, merge with equal stacks
// nearby, are picked up by players walking into them and despawn after a while. Only items in
// loaded chunks move and age; the others wait unchanged until their chunk loads again.
//
// Items below QcWorld::voidLimitY are removed, and drops there never appear (decision E-288).
//
// The number of items is bounded: above `activeLimit` awake items in loaded chunks new drops wait
// in a queue and appear as soon as older items are picked up or despawn. A chunk that loads with
// too many items keeps the surplus as dormant items (frozen, invisible) that wake up oldest first.
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
    static constexpr int activeLimit = 512;
    static constexpr int queueLimit = 1024;
    static constexpr int totalLimit = 4096;

    struct Item
    {
        quint32 id = 0;
        QcItemStack stack;
        QcEntityMotion motion;
        double age = 0.0;
        double pickupDelay = 0.0;
        // Lying still on the ground: skipped by physics until the block below changes.
        bool resting = false;
        // Surplus item in a loaded chunk beyond activeLimit: frozen and not sent to clients.
        bool dormant = false;
    };

    struct Drop
    {
        QcItemStack stack;
        QcVec3 position;
        QcVec3 velocity;
        double pickupDelay = QcItemEntityManager::pickupDelay;
        double age = 0.0;
    };

    struct PlayerView
    {
        quint32 entityId = 0;
        QcVec3 position;
        bool alive = true;
    };

    // Called when an item touches a player; returns how many items did not fit into the inventory.
    using GiveFunction = std::function<int(quint32 playerEntityId, QcItemStack stack)>;
    using LoadedFunction = std::function<bool(const QcVec3 &position)>;

    QcItemEntityManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    // Adds a drop, or queues it when too many items are active. Returns the new id, 0 when queued,
    // empty or in the void.
    quint32 spawn(const Drop &drop, quint32 id);
    // Drops a stack at `position` with a small random upward velocity (mined blocks, loot).
    quint32 spawnScattered(QcItemStack stack, const QcVec3 &position, quint32 id);

    // What a tick asks the server.
    struct TickCallbacks
    {
        LoadedFunction isLoaded;
        GiveFunction give;
        std::function<quint32()> nextEntityId;
    };

    // One tick: physics, pickup, merging, ageing and despawning. Returns the items that despawned
    // of old age (the caller may plant saplings from them).
    QList<Item> tick(const QcBlockAccess &world, const QList<PlayerView> &players, double dt, const TickCallbacks &callbacks);

    const QList<Item> &items() const { return m_items; }
    // For moving items from outside their physics (pistons push them, decision E-337).
    QList<Item> &editableItems() { return m_items; }
    const QList<Drop> &queued() const { return m_queue; }
    // Awake items in loaded chunks after the last tick (plus drops spawned since).
    int activeCount() const { return m_activeCount; }
    // Items removed because a limit was exceeded since the start.
    qint64 discardedCount() const { return m_discarded; }
    void clear();
    // Takes one item off the stack of item entity `id` (a seed planting itself); the entity goes when
    // its stack is empty. False when there is no such awake item.
    bool takeOne(quint32 id);
    // Removes the awake items that match (burning in lava); returns how many.
    qsizetype removeWhere(const std::function<bool(const Item &)> &match);
    // Replaces all items, e.g. from a save; ids are assigned by `nextEntityId`.
    void restore(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId);

private:
    void step(Item &item, const QcBlockAccess &world, double dt);
    void pickUp(Item &item, const QList<PlayerView> &players, const GiveFunction &give);
    // Drops emptied items and items in the void, collects the ones that despawned of old age and recounts the awake ones.
    QList<Item> removeFinished(const QList<bool> &awake);
    void spawnQueued(const TickCallbacks &callbacks);
    void merge(const QList<bool> &active);
    void enqueue(const Drop &drop);
    void enforceTotalLimit(const LoadedFunction &isLoaded);
    void balanceDormancy(const QList<bool> &loaded);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator m_random;
    QList<Item> m_items;
    QList<Drop> m_queue;
    int m_activeCount = 0;
    quint64 m_ticks = 0;
    qint64 m_discarded = 0;
};
