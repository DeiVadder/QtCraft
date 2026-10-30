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

/*! \brief The village plans of the regions planned last (decision E-416), shared by the generation
    threads and the server thread.

    When it is full it lets go of the region used least recently; a plan that someone else still
    holds (QcVillages keeps the plans of the villages with loaded chunks) is found again through
    a weak reference instead of being planned anew. Regions without a village are cached as null
    plans. Thread-safe. */
class QcVillagePlanCache
{
public:
    /*! Plans the village of a region; null when the region has none. */
    using Planner = std::function<std::shared_ptr<const QcVillagePlan>(int regionX, int regionZ)>;

    /*! Keeps the plans of at most \a capacity regions. */
    explicit QcVillagePlanCache(qsizetype capacity);

    /*! Returns the plan of region \a regionX, \a regionZ: cached, held elsewhere, or planned
        with \a planner outside the lock (two threads planning the same region at once both plan
        it; the first one stored is kept). */
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> plan(int regionX, int regionZ, const Planner &planner) const;
    /*! Returns the plan of region \a regionX, \a regionZ when it is cached or held elsewhere (a
        null plan: no village); std::nullopt when it is not planned yet. Never plans. */
    [[nodiscard]] std::optional<std::shared_ptr<const QcVillagePlan>> planned(int regionX, int regionZ) const;

    /*! Returns how many regions the cache keeps at most. */
    qsizetype capacity() const { return m_capacity; }
    /*! Returns how many regions the cache keeps now (not counting plans only held elsewhere). */
    qsizetype size() const;
    /*! Returns how often a region was planned so far, repeats included (tests count how often a
        region is planned again). */
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
