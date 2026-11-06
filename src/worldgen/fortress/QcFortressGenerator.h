// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWorldLabel.h"
#include "worldgen/fortress/QcFortressBlocks.h"
#include "worldgen/fortress/QcFortressBuilder.h"
#include "worldgen/fortress/QcFortressPlanner.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcLocatedPlace.h"
#include "worldgen/generators/QcStructureSites.h"

#include <QHash>

#include <memory>
#include <mutex>
#include <optional>

/*! \brief The fortresses of the nether in its chunks (decisions E-1070 to E-1075): plans cached by
    region (QcFortressPlanner), each chunk writing its own cells (QcFortressBuilder) in any order
    with the same result, with the spawner's kind, the loot of the chests, the spawn area and
    /locate from the plans alone. Thread-safe, after keepClearOf() and setDemonstration(). */
class QcFortressGenerator
{
public:
    /*! Generates from \a definitions and \a terrain, which outlive it, with the blocks of
        \a content; the places follow \a seed. */
    QcFortressGenerator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcContentRegistry &content, quint64 seed);

    /*! Returns what could not be resolved. */
    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }
    /*! Keeps fortresses out of \a area: a plan reaching into it is not built. */
    void keepClearOf(QcXzRect area);
    /*! Stands a fortress in a demonstration world with the west edge of its columns at the x of
        \a corner, its first lattice row at the z of it and its lower floor at the y. */
    void setDemonstration(QcBlockPos corner);
    /*! Returns the demonstration's plan; null without one. */
    [[nodiscard]] const std::shared_ptr<const QcFortressPlan> &demonstration() const { return m_demonstration; }

    /*! Returns the plan of the region \a regionX, \a regionZ that is built, planned once; null
        without one. */
    [[nodiscard]] std::shared_ptr<const QcFortressPlan> planOf(int regionX, int regionZ) const;
    /*! Returns the built plans touching \a area. */
    [[nodiscard]] QList<std::shared_ptr<const QcFortressPlan>> plansIn(QcXzRect area) const;
    /*! Writes the cells of the fortresses touching \a chunk into it. */
    void build(QcChunk &chunk) const;
    /*! Returns the loot of the chests and the kind of the spawners in the chunk \a pos. */
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;
    /*! Returns the spawn areas within about \a reach blocks of \a from. */
    [[nodiscard]] QcStructureSites sitesNear(QcBlockPos from, int reach) const;
    /*! Returns sitesNear() of \a from from the plans made so far, never planning; none when a
        region within \a reach is not planned yet. */
    [[nodiscard]] std::optional<QcStructureSites> plannedSitesNear(QcBlockPos from, int reach) const;
    /*! Returns the labels of the demonstration's rooms in the chunk \a pos: its name over the
        middle room, the spawner and the wart beds; none in a natural fortress. */
    [[nodiscard]] QList<QcWorldLabel> labelsIn(QcChunkPos pos) const;
    /*! Returns the fortress nearest to \a from within about \a reach blocks. */
    [[nodiscard]] QcLocateResult nearest(QcBlockPos from, int reach) const;

private:
    // Calls `visit` with each region touching `area`.
    template <typename Visit>
    void forRegions(QcXzRect area, Visit visit) const;
    [[nodiscard]] static qint64 keyOf(int regionX, int regionZ);
    // The spawn area of `plan`.
    [[nodiscard]] QcSpawnArea areaOf(const QcFortressPlan &plan) const;
    [[nodiscard]] bool isBuilt(const QcFortressPlan &plan) const;
    [[nodiscard]] const QcFortressPlanner &planner() const { return m_planner; }
    [[nodiscard]] bool hasBuilder() const { return m_builder.has_value(); }
    // Only called where a builder exists (the blocks resolved).
    [[nodiscard]] const QcFortressBuilder &builder() const { return *m_builder; } // NOLINT(bugprone-unchecked-optional-access)
    void setDemonstrationPlan(std::shared_ptr<const QcFortressPlan> plan);
    [[nodiscard]] const QList<QcXzRect> &clearAreas() const;
    // The plans by region; the caller holds m_mutex.
    [[nodiscard]] QHash<qint64, std::shared_ptr<const QcFortressPlan>> &plans() const { return m_plans; }
    // The plan cached under `key`, if any (a null plan: a region without a fortress); the caller
    // holds m_mutex.
    [[nodiscard]] std::optional<std::shared_ptr<const QcFortressPlan>> cachedPlan(qint64 key) const;
    // Caches `plan` under `key`, emptying a full cache first, and returns it; the caller holds
    // m_mutex.
    std::shared_ptr<const QcFortressPlan> cachePlan(qint64 key, std::shared_ptr<const QcFortressPlan> plan) const;

    QcFortressPlanner m_planner;
    std::optional<QcFortressBlocks> m_blocks;
    std::optional<QcFortressBuilder> m_builder;
    QStringList m_warnings;
    quint64 m_lootSeed = 0;
    // Set up by keepClearOf() before any thread reads it.
    QList<QcXzRect> m_clear;
    std::shared_ptr<const QcFortressPlan> m_demonstration;
    // Every thread that generates or plans hands the plans to the others under this mutex.
    mutable std::mutex m_mutex;
    mutable QHash<qint64, std::shared_ptr<const QcFortressPlan>> m_plans;
};
