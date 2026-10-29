// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QMutex>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

struct QcVillagePlan;

// The village plans of the regions planned last (decision E-416), shared by the generation threads
// and the server thread. When it is full it lets go of the region used least recently; a plan that
// someone else still holds (QcVillages keeps the plans of the villages with loaded chunks) is found
// again through a weak reference instead of being planned anew. Regions without a village are cached
// as null plans. Thread-safe.
class QcVillagePlanCache
{
public:
    using Planner = std::function<std::shared_ptr<const QcVillagePlan>(int regionX, int regionZ)>;

    explicit QcVillagePlanCache(qsizetype capacity);

    // The region's plan: cached, held elsewhere, or planned with `planner` outside the lock (two
    // threads planning the same region at once both plan it; the first one stored is kept).
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> plan(int regionX, int regionZ, const Planner &planner) const;
    // The region's plan when it is cached or held elsewhere (a null plan: no village); std::nullopt
    // when it is not planned yet. Never plans.
    [[nodiscard]] std::optional<std::shared_ptr<const QcVillagePlan>> planned(int regionX, int regionZ) const;

    qsizetype capacity() const { return m_capacity; }
    qsizetype size() const;
    // Regions planned so far (tests count how often a region is planned again).
    quint64 planned() const { return m_planned.load(std::memory_order_relaxed); }

private:
    struct Entry
    {
        std::shared_ptr<const QcVillagePlan> plan;
        quint64 used = 0;
    };

    // Looks the region up in the cache or among the held plans; called with the lock held.
    std::optional<std::shared_ptr<const QcVillagePlan>> findLocked(qint64 key) const;
    // Stores the plan, letting go of the least recently used region when the cache is full.
    void insertLocked(qint64 key, std::shared_ptr<const QcVillagePlan> plan) const;
    void evictOldestLocked() const;

    qsizetype m_capacity;
    mutable QMutex m_mutex;
    mutable QHash<qint64, Entry> m_entries;
    // Plans the cache let go of; found again while someone still holds them.
    mutable QHash<qint64, std::weak_ptr<const QcVillagePlan>> m_released;
    mutable quint64 m_clock = 0;
    mutable std::atomic<quint64> m_planned{0};
};
