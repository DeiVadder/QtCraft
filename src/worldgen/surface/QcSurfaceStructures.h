// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcStructureSites.h"
#include "worldgen/surface/QcSurfaceBlend.h"
#include "worldgen/surface/QcSurfacePlanner.h"
#include "worldgen/terrain/QcTerrainColumnGrid.h"

#include <QHash>

#include <memory>
#include <mutex>
#include <optional>

class QcVillageGenerator;

/*! \brief The structures the terrain generator places by itself on the surface (decision E-590, the
    desert temple): each kind plans its regions once (QcSurfacePlanner, plans cached), a copy lies
    wholly in its region, so a chunk asks only the one region of each kind it lies in.

    Writing a copy into a chunk clears the terrain above its footprint (from its layer
    \c {clearFrom} to \c {clearance} blocks over its top; over water never at or below the sea,
    decision E-622; not for kinds without \c {clearTerrain}, decision E-630), blends the ground
    around it to its floor (the rule's \c {blend}, QcSurfaceBlend, decision E-889), fills the
    ground below it with the kind's foundation down to solid ground and writes its cells; the
    same in any order of the chunks. No trees, plants or surface pools grow near a copy
    (footprintsNear()), so none is cut or left floating. Its loot comes with furnishingIn(), its
    guardian zones with sitesNear(). Thread-safe. */
class QcSurfaceStructures
{
public:
    /*! Blocks cleared above the top of a copy. */
    static constexpr int clearance = 4;
    /*! The deepest foundation below a copy in blocks. */
    static constexpr int maxFoundation = 16;
    /*! Regions whose plans are kept at most; beyond, the cache starts over. */
    static constexpr qsizetype cacheCapacity = 4096;

    /*! \brief The cache key of a kind's region (decision E-883): the kind and both region
        coordinates in full, 32 bits each, so no two regions of the world share a key however far
        out they lie. */
    struct PlanKey
    {
        /*! The kind. */
        QcSurfacePlanner::KindId kind;
        /*! The region x in the upper 32 bits, the region z in the lower 32 bits. */
        qint64 region = 0;
        bool operator==(const PlanKey &) const = default;
        friend size_t qHash(PlanKey key, size_t seed = 0) noexcept { return qHashMulti(seed, key.kind.index(), key.region); }
    };

    /*! Plans the kinds of \a content with \a seed on the columns of \a terrain, clear of the
        villages of \a villages. */
    QcSurfaceStructures(const QcTerrainGenerator &terrain, const QcVillageGenerator &villages, const QcContentRegistry &content, quint64 seed);

    /*! Returns the cache key of region (\a regionX, \a regionZ) of kind \a kind. */
    [[nodiscard]] static PlanKey planKey(QcSurfacePlanner::KindId kind, int regionX, int regionZ);

    /*! Returns the planner of the kinds. */
    const QcSurfacePlanner &planner() const { return m_planner; }
    /*! Returns the copy of kind \a kind in region (\a regionX, \a regionZ), planned once; null
        when the region has none. */
    [[nodiscard]] std::shared_ptr<const QcStructurePlacement> planOf(QcSurfacePlanner::KindId kind, int regionX, int regionZ) const;
    /*! Returns planOf() of kind \a kind in region (\a regionX, \a regionZ) when the terrain
        builds it (QcWorldGenerator::buildsStructure with reachOf()), else null: what the chunks,
        their furnishing, the sites and the searches see. */
    [[nodiscard]] std::shared_ptr<const QcStructurePlacement> builtPlanOf(QcSurfacePlanner::KindId kind, int regionX, int regionZ) const;
    /*! Returns the columns the copy \a plan of kind \a kind changes: its footprint and blended
        ground, and the column grids of the chunks whose heights the blend moves. */
    [[nodiscard]] QcXzRect reachOf(const QcStructurePlacement &plan, QcSurfacePlanner::KindId kind) const;
    /*! Returns reachOf() of every built copy whose reach overlaps the chunk \a pos. */
    [[nodiscard]] QList<QcXzRect> reachesTouching(QcChunkPos pos) const;
    /*! Returns the copies of every kind whose footprint or blended ground overlaps the chunk
        \a pos, built or not (planning). */
    QList<std::shared_ptr<const QcStructurePlacement>> plansTouching(QcChunkPos pos) const;
    /*! Returns the same as plansTouching() for \a pos, only the built copies (builtPlanOf()). */
    QList<std::shared_ptr<const QcStructurePlacement>> builtPlansTouching(QcChunkPos pos) const;
    /*! Returns the footprints of the copies of every kind, grown by their blended ground, within
        \a margin columns of the chunk \a pos: the terrain generator places no features and no
        surface pools there (decisions E-630, E-889). */
    QList<QcXzRect> footprintsNear(QcChunkPos pos, int margin) const;
    /*! Returns the same as footprintsNear() for \a pos and \a margin where no trees or plants
        grow: kinds with \c {blendVegetation} keep only
        the ring right beside their walls bare, the others their whole blended ground (decision
        E-889). */
    QList<QcXzRect> bareNear(QcChunkPos pos, int margin) const;
    /*! \brief A column whose ground the blend moves: its new top block. */
    struct Moved
    {
        /*! The x, z of the column. \qcshared */
        int x = 0;
        int z = 0;
        /*! The new top block. */
        int top = 0;
    };
    /*! Blends the ground of \a chunk around the copies touching it (before its features). */
    void blend(QcChunk &chunk) const;
    /*! Returns the columns of \a columns, the column grid around the chunk \a pos, whose ground
        blend() moves (in the chunk and its
        neighbours alike), in the order blend() shapes them: where two blends meet, the later one
        wins in both. */
    [[nodiscard]] QList<Moved> blendedGround(const QcTerrainColumnGrid &columns, QcChunkPos pos) const;
    /*! Clears the terrain over the copies touching \a chunk, lays their foundations and writes
        them into it. */
    void build(QcChunk &chunk) const;
    /*! Returns the loot of the copies' chests in the chunk \a pos. */
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;
    /*! Returns the guardian zones and spawn areas of the copies in the regions within \a reach
        of \a from. */
    QcStructureSites sitesNear(QcBlockPos from, int reach) const;
    /*! Returns the same as sitesNear() for \a from and \a reach from the regions planned so far,
        never planning (the server's tick thread); std::nullopt when a region within \a reach is
        not planned yet. */
    [[nodiscard]] std::optional<QcStructureSites> plannedSitesNear(QcBlockPos from, int reach) const;
    /*! Returns the copy of kind \a name nearest to \a from within \a reach blocks, if any. */
    [[nodiscard]] std::optional<QcStructurePlacement> nearest(const QString &name, QcBlockPos from, int reach) const;

private:
    // The copy of kind `kind` in the region when the region is planned; std::nullopt otherwise.
    [[nodiscard]] std::optional<std::shared_ptr<const QcStructurePlacement>> plannedOf(QcSurfacePlanner::KindId kind, int regionX, int regionZ) const;
    // The sites of the regions within `reach` of `from`; with `mayPlan` false only from
    // planned regions (std::nullopt when one is not planned yet).
    [[nodiscard]] std::optional<QcStructureSites> collectSites(QcBlockPos from, int reach, bool mayPlan) const;
    // The footprints within `margin` of the chunk grown by their blend, or with `plantsOnBlend` by
    // the bare ring of kinds with `blendVegetation`.
    [[nodiscard]] QList<QcXzRect> grownFootprintsNear(QcChunkPos pos, int margin, bool plantsOnBlend) const;
    // The columns of `grid` (within the column grid) whose ground the blend around `plan` moves.
    [[nodiscard]] QList<Moved> movedAround(const QcStructurePlacement &plan, int blend, const QcTerrainColumnGrid &columns,
                                           QcXzRect grid) const;
    // Whether a village or a copy of a kind before `kind` lies within `margin` of the footprint, or
    // within the larger blend of the two (decision E-889).
    bool occupied(QcSurfacePlanner::KindId kind, QcXzRect footprint, int margin) const;
    // planOf() or builtPlanOf().
    using PlanGetter = std::shared_ptr<const QcStructurePlacement> (QcSurfaceStructures::*)(QcSurfacePlanner::KindId, int, int) const;
    // The copies `plans` gives whose footprint or blended ground overlaps the chunk `pos`.
    [[nodiscard]] QList<std::shared_ptr<const QcStructurePlacement>> touchingWith(QcChunkPos pos, PlanGetter plans) const;
    void build(QcChunk &chunk, const QcStructurePlacement &placement, const QcSurfacePlanner::Kind &kind) const;

    const QcTerrainGenerator &m_terrain;
    const QcVillageGenerator &m_villages;
    QcSurfacePlanner m_planner;
    QcSurfaceBlend m_blend;
    // Per block state: ground a foundation stops at (solid and not a liquid).
    std::vector<bool> m_ground;
    // Every thread that generates or plans with this generator (the server's generation pool,
    // headless tools, the world map preview's worker) hands the plans to the others under this
    // mutex.
    mutable std::mutex m_mutex;
    // By kind and region (planKey).
    // NOLINT(qc-member-access): read and written only under m_mutex; an accessor would hide that the lock must be held
    mutable QHash<PlanKey, std::shared_ptr<const QcStructurePlacement>> m_plans;
};
