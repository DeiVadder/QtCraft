// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWorldLabel.h"
#include "worldgen/bastion/QcBastionBlocks.h"
#include "worldgen/bastion/QcBastionBuilder.h"
#include "worldgen/bastion/QcBastionPlanner.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcLocatedPlace.h"
#include "worldgen/generators/QcRegionPlans.h"
#include "worldgen/generators/QcStructureSites.h"

#include <memory>
#include <optional>

class QcNetherComplexes;

/*! \brief The bastion ruins of the nether in its chunks (decisions E-1087 to E-1089): plans cached
    by region, each chunk writing its own cells in any order with the same result, with the loot
    of the chests, the spawners, those who stand there from the start, the spawn area and /locate
    from the plans alone. Thread-safe, after keepClearOf() and setDemonstration(). */
class QcBastionGenerator
{
public:
    /*! Generates from \a definitions, \a terrain and \a complexes, which outlive it, with the
        blocks of \a content; the forms follow \a seed. */
    QcBastionGenerator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes,
                       const QcContentRegistry &content, quint64 seed);

    /*! Returns what could not be resolved. */
    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }
    /*! Keeps bastions out of \a area: a plan reaching into it is not built. */
    void keepClearOf(QcXzRect area);
    /*! Stands each form in a demonstration world, in a row along z around \a middle. */
    void setDemonstration(QcBlockPos middle);
    /*! Returns the demonstration's plans, one per form; none without one. */
    [[nodiscard]] const QList<std::shared_ptr<const QcBastionPlan>> &demonstrations() const { return plans().demonstrations(); }

    /*! Returns the plan of the region \a regionX, \a regionZ that is built, planned once; null
        without one. */
    [[nodiscard]] std::shared_ptr<const QcBastionPlan> planOf(int regionX, int regionZ) const { return plans().planOf(regionX, regionZ); }
    /*! Returns the built plans touching \a area. */
    [[nodiscard]] QList<std::shared_ptr<const QcBastionPlan>> plansIn(QcXzRect area) const { return plans().plansIn(area); }
    /*! Writes the cells of the bastions touching \a chunk into it. */
    void build(QcChunk &chunk) const;
    /*! Returns the loot of the chests, the kind of the spawners and those who stand in the chunk
        \a pos from the start. */
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;
    /*! Returns whether someone stands in the chunk \a pos from the start. */
    [[nodiscard]] bool hasResidentsIn(QcChunkPos pos) const;
    /*! Returns the spawn areas within about \a reach blocks of \a from; none when the bastions have
        no kinds of their own. */
    [[nodiscard]] QcStructureSites sitesNear(QcBlockPos from, int reach) const;
    /*! Returns sitesNear() of \a from from the plans made so far, never planning; none when a
        region within \a reach is not planned yet. */
    [[nodiscard]] std::optional<QcStructureSites> plannedSitesNear(QcBlockPos from, int reach) const;
    /*! Returns whether a part of a bastion holds the cell \a at, from the plans made so far,
        never planning (decision E-1160); none while its region is not planned yet. */
    [[nodiscard]] std::optional<bool> plannedHolds(QcBlockPos at) const;
    /*! Returns the signs of the demonstration's forms in the chunk \a pos. */
    [[nodiscard]] QList<QcWorldLabel> labelsIn(QcChunkPos pos) const;
    /*! Returns the bastion nearest to \a from within about \a reach blocks. */
    [[nodiscard]] QcLocateResult nearest(QcBlockPos from, int reach) const;

private:
    [[nodiscard]] const QcBastionPlanner &planner() const { return m_planner; }
    [[nodiscard]] const QcRegionPlans<QcBastionPlan> &plans() const { return m_plans; }
    [[nodiscard]] bool hasBuilder() const { return m_builder.has_value(); }
    // Only called where a builder exists (the blocks resolved).
    [[nodiscard]] const QcBastionBuilder &builder() const { return *m_builder; } // NOLINT(bugprone-unchecked-optional-access)
    // The content name of the table of `loot`, and of `folk`.
    [[nodiscard]] const QString &tableOf(QcBastionPlan::Loot loot) const;
    [[nodiscard]] const QString &nameOf(QcBastionPlan::Folk folk) const;
    // The spawn areas of `plans`.
    [[nodiscard]] QcStructureSites sitesOf(const QList<std::shared_ptr<const QcBastionPlan>> &plans) const;

    QcBastionPlanner m_planner;
    std::optional<QcBastionBlocks> m_blocks;
    std::optional<QcBastionBuilder> m_builder;
    QcRegionPlans<QcBastionPlan> m_plans;
    QStringList m_warnings;
    quint64 m_lootSeed = 0;
};
