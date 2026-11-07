// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWorldLabel.h"
#include "worldgen/fortress/QcFortressBlocks.h"
#include "worldgen/fortress/QcFortressBuilder.h"
#include "worldgen/fortress/QcFortressPlanner.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcLocatedPlace.h"
#include "worldgen/generators/QcRegionPlans.h"
#include "worldgen/generators/QcStructureSites.h"

#include <memory>
#include <optional>

class QcNetherComplexes;

/*! \brief The fortresses of the nether in its chunks (decisions E-1070 to E-1075): plans cached by
    region (QcFortressPlanner), each chunk writing its own cells (QcFortressBuilder) in any order
    with the same result, with the spawner's kind, the loot of the chests, the spawn area and
    /locate from the plans alone. Thread-safe, after keepClearOf() and setDemonstration(). */
class QcFortressGenerator
{
public:
    /*! Generates from \a definitions, \a terrain and \a complexes, which outlive it, with the
        blocks of \a content; the places follow \a seed. */
    QcFortressGenerator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes,
                        const QcContentRegistry &content, quint64 seed);

    /*! Returns what could not be resolved. */
    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }
    /*! Keeps fortresses out of \a area: a plan reaching into it is not built. */
    void keepClearOf(QcXzRect area);
    /*! Stands a fortress in a demonstration world with the west edge of its columns at the x of
        \a corner, its first lattice row at the z of it and its lower floor at the y. */
    void setDemonstration(QcBlockPos corner);
    /*! Returns the demonstration's plan; null without one. */
    [[nodiscard]] std::shared_ptr<const QcFortressPlan> demonstration() const;

    /*! Returns the plan of the region \a regionX, \a regionZ that is built, planned once; null
        without one. */
    [[nodiscard]] std::shared_ptr<const QcFortressPlan> planOf(int regionX, int regionZ) const { return plans().planOf(regionX, regionZ); }
    /*! Returns the built plans touching \a area. */
    [[nodiscard]] QList<std::shared_ptr<const QcFortressPlan>> plansIn(QcXzRect area) const { return plans().plansIn(area); }
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
    // The spawn area of `plan`.
    [[nodiscard]] QcSpawnArea areaOf(const QcFortressPlan &plan) const;
    [[nodiscard]] const QcFortressPlanner &planner() const { return m_planner; }
    [[nodiscard]] const QcRegionPlans<QcFortressPlan> &plans() const { return m_plans; }
    [[nodiscard]] bool hasBuilder() const { return m_builder.has_value(); }
    // Only called where a builder exists (the blocks resolved).
    [[nodiscard]] const QcFortressBuilder &builder() const { return *m_builder; } // NOLINT(bugprone-unchecked-optional-access)

    QcFortressPlanner m_planner;
    std::optional<QcFortressBlocks> m_blocks;
    std::optional<QcFortressBuilder> m_builder;
    QcRegionPlans<QcFortressPlan> m_plans;
    QStringList m_warnings;
    quint64 m_lootSeed = 0;
};
