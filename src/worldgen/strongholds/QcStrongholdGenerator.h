// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcLocatedPlace.h"
#include "worldgen/strongholds/QcStrongholdBlocks.h"
#include "worldgen/strongholds/QcStrongholdBuilder.h"
#include "worldgen/strongholds/QcStrongholdPlanner.h"
#include "worldgen/strongholds/QcStrongholdRings.h"

#include <QHash>

#include <memory>
#include <mutex>
#include <optional>

class QcTerrainGenerator;

/*! \brief The strongholds of the terrain in its chunks (decisions E-1170 to E-1174), from
    generator version 4 on (the terrain asks only then): their places on the rings
    (QcStrongholdRings), a plan per stronghold planned once and cached (QcStrongholdPlanner), each
    chunk writing its own cells (QcStrongholdBuilder) in any order with the same result, and the
    loot, the spawner, /locate and the area for advancements from the plans alone.

    A plan is made outside the lock and published whole under it, like the plans of the regions
    of other structures (QcRegionPlans); the strongholds stand on rings rather than in regions,
    so the cache is kept by the stronghold's number. Thread-safe. */
class QcStrongholdGenerator
{
public:
    /*! The first overworld generator version with strongholds (decision E-1170). */
    static constexpr int firstVersion = 4;

    /*! Generates in \a terrain, which outlives it, with the blocks of \a content; the places
        and rolls follow \a seed. */
    QcStrongholdGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    /*! Returns what could not be resolved. */
    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }
    /*! Returns whether the content asks for strongholds and has their blocks. */
    [[nodiscard]] bool enabled() const { return m_builder.has_value(); }
    /*! Returns the places of the strongholds. */
    [[nodiscard]] const QcStrongholdRings &rings() const { return m_rings; }
    /*! Returns the plan of the stronghold \a index, planned once; null when disabled. */
    [[nodiscard]] std::shared_ptr<const QcStrongholdPlan> planOf(int index) const;
    /*! Returns the plans touching \a area that the terrain builds (QcWorldGenerator::
        buildsStructure with their bounds), planning them. */
    [[nodiscard]] QList<std::shared_ptr<const QcStrongholdPlan>> plansIn(QcXzRect area) const;
    /*! Writes the cells of the strongholds touching \a chunk into it. */
    void build(QcChunk &chunk) const;
    /*! Returns the loot of the chests and the kind of the spawners in the chunk \a pos. */
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;
    /*! Returns whether a piece of a stronghold holds the cell \a at, from the plans and rings
        made so far, never planning; none while one near is not planned yet. */
    [[nodiscard]] std::optional<bool> plannedHolds(QcBlockPos at) const;
    /*! Plans the strongholds within \a reach blocks of \a from, so that plannedHolds() knows
        them. */
    void planNear(QcBlockPos from, int reach) const;
    /*! Returns the foot of the starting staircase of the stronghold nearest to \a from that the
        terrain builds, however far; \a reach is only reported as searched. */
    [[nodiscard]] QcLocateResult nearest(QcBlockPos from, int reach) const;

    /*! Returns how far a stronghold's pieces reach from its place at most: the margin round an
        area within which plansIn() looks for strongholds. */
    [[nodiscard]] int extent() const;

private:
    // The plan of `index` if made so far.
    [[nodiscard]] std::optional<std::shared_ptr<const QcStrongholdPlan>> cached(int index) const;
    [[nodiscard]] const QcStrongholdPlanner &planner() const { return m_planner; }
    // Only called where a builder exists (the blocks resolved).
    [[nodiscard]] const QcStrongholdBuilder &builder() const { return *m_builder; } // NOLINT(bugprone-unchecked-optional-access)
    [[nodiscard]] QString lootTable(QcStrongholdPlan::Loot loot) const;

    const QcTerrainGenerator &m_terrain;
    QcStrongholdRings m_rings;
    QcStrongholdPlanner m_planner;
    std::optional<QcStrongholdBlocks> m_blocks;
    std::optional<QcStrongholdBuilder> m_builder;
    QStringList m_warnings;
    quint64 m_lootSeed = 0;
    // Every thread that plans hands the plans to the others under this mutex.
    mutable std::mutex m_mutex;
    mutable QHash<int, std::shared_ptr<const QcStrongholdPlan>> m_plans; // NOLINT(qc-member-access): read and written only in cached() and planOf()
};
