// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcIntMath.h"
#include "common/core/QcXzRect.h"
#include "worldgen/generators/QcLocatedPlace.h"

#include <QHash>
#include <QList>

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

/*! \brief The plans of type \a Plan of a structure that stands wholly inside square regions, at
    most one each
    (decisions E-1070, E-1087): planned once, cached with a mutex, left out where an area is kept
    clear, and joined by the plans a demonstration world stands by hand.

    \c {Plan} has \c {touches(QcXzRect)}, \c {bounds} and \c {middle()}. Thread-safe, after
    keepClearOf() and addDemonstration(). */
template <typename Plan>
class QcRegionPlans
{
public:
    /*! The plan of a region; null for none. */
    using Pointer = std::shared_ptr<const Plan>;
    /*! Plans the region it is given, from the seed and the terrain alone. */
    using Planner = std::function<Pointer(int regionX, int regionZ)>;

    /*! Keeps the plans of regions of \a regionSize blocks made by \a planner; a \a regionSize of 0
        means there are none. */
    QcRegionPlans(int regionSize, Planner planner)
        : m_regionSize(regionSize)
        , m_planner(std::move(planner))
    {
    }

    /*! Sets \a regionSize and \a planner, before any thread asks for a plan. */
    void setPlanner(int regionSize, Planner planner)
    {
        m_regionSize = regionSize;
        m_planner = std::move(planner);
    }

    /*! Keeps plans out of \a area: one reaching into it is not built. */
    void keepClearOf(QcXzRect area) { m_clear.append(area); }
    /*! Stands \a plan in the world besides those of the regions. */
    void addDemonstration(Pointer plan) { m_demonstrations.append(std::move(plan)); }
    /*! Returns the plans stood by hand. */
    [[nodiscard]] const QList<Pointer> &demonstrations() const { return m_demonstrations; }

    /*! Returns the plan of the region \a regionX, \a regionZ that is built, planned once; null
        without one. */
    [[nodiscard]] Pointer planOf(int regionX, int regionZ) const
    {
        if (regionSize() <= 0) {
            return nullptr;
        }
        const qint64 key = keyOf(regionX, regionZ);
        {
            const std::scoped_lock lock(m_mutex);
            if (const std::optional<Pointer> cached = cachedPlan(key)) {
                return *cached;
            }
        }
        Pointer plan = m_planner(regionX, regionZ);
        if (plan && !isBuilt(*plan)) {
            plan = nullptr;
        }
        const std::scoped_lock lock(m_mutex);
        return cachePlan(key, std::move(plan));
    }

    /*! Returns the built plans touching \a area. */
    [[nodiscard]] QList<Pointer> plansIn(QcXzRect area) const
    {
        QList<Pointer> plans;
        forRegions(area, [&](int regionX, int regionZ) {
            Pointer plan = planOf(regionX, regionZ);
            if (plan && plan->touches(area)) {
                plans.append(std::move(plan));
            }
        });
        return plans + demonstrationsIn(area);
    }

    /*! Returns plansIn() of \a area from the plans made so far, never planning; none when a region
        of it is not planned yet. */
    [[nodiscard]] std::optional<QList<Pointer>> plannedIn(QcXzRect area) const
    {
        QList<Pointer> plans;
        bool planned = true;
        {
            const std::scoped_lock lock(m_mutex);
            forRegions(area, [&](int regionX, int regionZ) {
                const std::optional<Pointer> cached = cachedPlan(keyOf(regionX, regionZ));
                if (!cached) {
                    planned = false;
                } else if (*cached && (*cached)->touches(area)) {
                    plans.append(*cached);
                }
            });
        }
        return planned ? std::optional(plans + demonstrationsIn(area)) : std::nullopt;
    }

    /*! Returns the plan nearest to \a from within about \a reach blocks, asking at most
        \a maxRegions regions. */
    [[nodiscard]] QcLocateResult nearest(QcBlockPos from, int reach, qsizetype maxRegions) const
    {
        if (regionSize() <= 0) {
            return {std::nullopt, reach};
        }
        const auto placeOf = [this](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
            const Pointer plan = planOf(regionX, regionZ);
            return plan ? std::optional(QcLocatedPlace{plan->middle(), std::nullopt}) : std::nullopt;
        };
        QcLocateResult result = QcPlaceSearch::nearestInRegions(from, reach, regionSize(), maxRegions, placeOf);
        for (const Pointer &plan : demonstrations()) {
            const QcBlockPos middle = plan->middle();
            if (!result.place || QcPlaceSearch::distance(middle, from) < QcPlaceSearch::distance(result.place->at, from)) {
                result.place = QcLocatedPlace{middle, std::nullopt};
            }
        }
        return result;
    }

private:
    // Plans kept: more regions than a game visits at once.
    static constexpr qsizetype cacheCapacity = 256;

    [[nodiscard]] static qint64 keyOf(int regionX, int regionZ)
    {
        return (static_cast<qint64>(regionX) << 32) ^ static_cast<qint64>(static_cast<quint32>(regionZ));
    }
    [[nodiscard]] int regionSize() const { return m_regionSize; }
    [[nodiscard]] bool isBuilt(const Plan &plan) const
    {
        return std::ranges::none_of(m_clear, [&plan](QcXzRect area) { return area.intersects(plan.bounds); });
    }
    // Calls `visit` with each region touching `area`.
    template <typename Visit>
    void forRegions(QcXzRect area, Visit visit) const
    {
        if (regionSize() <= 0) {
            return;
        }
        const int size = regionSize();
        for (int regionZ = qcFloorDiv(area.minZ, size); regionZ <= qcFloorDiv(area.maxZ, size); ++regionZ) {
            for (int regionX = qcFloorDiv(area.minX, size); regionX <= qcFloorDiv(area.maxX, size); ++regionX) {
                visit(regionX, regionZ);
            }
        }
    }
    [[nodiscard]] QList<Pointer> demonstrationsIn(QcXzRect area) const
    {
        QList<Pointer> plans;
        std::ranges::copy_if(demonstrations(), std::back_inserter(plans), [area](const Pointer &plan) { return plan->touches(area); });
        return plans;
    }
    // The plans by region; the caller holds m_mutex.
    [[nodiscard]] QHash<qint64, Pointer> &plans() const { return m_plans; }
    // The plan cached under `key`, if any (a null plan: a region without one); the caller holds
    // m_mutex.
    [[nodiscard]] std::optional<Pointer> cachedPlan(qint64 key) const
    {
        const auto it = plans().constFind(key);
        return it == plans().cend() ? std::nullopt : std::optional(*it);
    }
    // Caches `plan` under `key`, emptying a full cache first, and returns it; the caller holds
    // m_mutex.
    Pointer cachePlan(qint64 key, Pointer plan) const
    {
        if (plans().size() >= cacheCapacity) {
            plans().clear();
        }
        return *plans().insert(key, std::move(plan));
    }

    int m_regionSize = 0;
    Planner m_planner;
    // Set up before any thread reads them.
    QList<QcXzRect> m_clear;
    QList<Pointer> m_demonstrations;
    // Every thread that generates or plans hands the plans to the others under this mutex.
    mutable std::mutex m_mutex;
    mutable QHash<qint64, Pointer> m_plans;
};
