// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcStructureSites.h"
#include "worldgen/surface/QcSurfaceBlend.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillagePlanCache.h"
#include "worldgen/villages/QcVillagePlanner.h"
#include "worldgen/villages/QcVillageStyles.h"
#include "worldgen/villages/QcWetlandShaper.h"

#include <memory>

class QcTerrainGenerator;

/*! \brief The villages of a terrain world (decision E-403): plans each region's village once
    (cached, shared by the generation threads and the server thread, decision E-416) and writes the
    part of it that lies in a chunk.

    A chunk lies in exactly one region and a village never leaves its region, so a chunk needs
    only its own region's plan. Thread-safe. */
class QcVillageGenerator
{
public:
    /*! Regions the cache keeps (least recently used go first). A village's plan holds about 33
        KB (the largest about 55 KB: the structures of its varied buildings, its road cells) and
        about one region in six has one, so a full cache holds about 3 MB, at most about 28 MB
        (test_village_measure). It keeps the 13 x 13 regions /locate village searches and those
        around the players generating. */
    static constexpr qsizetype cachedPlans = 512;

    /*! Plans villages over \a terrain with the styles of \a content, drawing from \a seed.
        What the styles leave out is in styles().warnings(). */
    QcVillageGenerator(const QcTerrainGenerator &terrain, std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    /*! Returns the village of the region that holds \a chunk; null when it has none. */
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> planFor(QcChunkPos chunk) const;
    /*! Returns the village of the region \a regionX, \a regionZ from the cache, planning it
        there first if needed; null when the region has none. */
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> regionPlan(int regionX, int regionZ) const;
    /*! Returns the village whose centre lies nearest to \a from within \a reach blocks (whole
        regions), if any. */
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> nearest(QcBlockPos from, int reach) const;
    /*! Returns the village piece of structure \a name (the temple at a desert village's centre)
        whose middle lies nearest to \a from within \a reach blocks (whole regions), if any. */
    [[nodiscard]] std::optional<QcStructurePlacement> nearestPiece(const QString &name, QcBlockPos from, int reach) const;
    /*! Writes the parts of \a plan that lie in \a chunk: over water the wetland first
        (QcWetlandShaper), the blended ground around the centre, the doorsteps, roads into the
        ground's top block and their steps, jetties, boardwalks and bridges, then the pieces after
        clearing the terrain above their footprint and filling the ground below it. */
    void build(QcChunk &chunk, const QcVillagePlan &plan) const;
    /*! Returns the loot of the chests of \a plan's pieces in the chunk at \a pos (decision
        E-590: the temple at the centre of a desert village). */
    [[nodiscard]] static QcChunkFurnishing furnishingIn(QcChunkPos pos, const QcVillagePlan &plan);
    /*! Returns the guardian zones (decision E-591) and spawn areas (decision E-871) of the
        pieces of the villages in the regions within \a reach of \a from. */
    QcStructureSites sitesNear(QcBlockPos from, int reach) const;
    /*! Returns the same as sitesNear() for \a from and \a reach from the regions planned so
        far, never planning (the server's tick thread); std::nullopt when a region within
        \a reach is not planned yet. */
    [[nodiscard]] std::optional<QcStructureSites> plannedSitesNear(QcBlockPos from, int reach) const;
    /*! Returns the guardian zones and spawn areas of the pieces of the village \a plan. */
    static QcStructureSites sitesOf(const QcVillagePlan &plan);
    /*! Returns the village styles read from the content. */
    const QcVillageStyles &styles() const { return m_styles; }
    /*! Returns the shaper of the ground of villages over water. */
    const QcWetlandShaper &wetland() const { return m_wetland; }
    /*! Returns the cache of the region plans. */
    const QcVillagePlanCache &cache() const { return m_cache; }
    /*! Returns the edge of a village region in blocks (QcVillagePlanner::spacing()). */
    int spacing() const { return m_planner.spacing(); }

private:
    // The sites of the regions within `reach` of `from`; with `mayPlan` false only from
    // planned regions (std::nullopt when one is not planned yet).
    [[nodiscard]] std::optional<QcStructureSites> collectSites(QcBlockPos from, int reach, bool mayPlan) const;
    void buildPiece(QcChunk &chunk, const QcStructurePlacement &piece, const QcVillagePlan &plan) const;
    // Over water (decision E-407): whether a world column of a piece gets a post, every `spacing`
    // cells from its north west corner and at its far edges.
    static bool isStiltColumn(const QcStructurePlacement &piece, int x, int z, int spacing);
    // A post under a piece's floor block at `floor` (local in the chunk) down to the ground.
    void buildPost(QcChunk &chunk, QcBlockPos floor, QcBlockId post) const;
    // Clears the terrain above a piece's floor in one column of the chunk (x and z of `local`) and
    // fills the ground below it with the foundation (air: none) down to solid ground.
    void prepareColumn(QcChunk &chunk, QcBlockPos local, const QcStructurePlacement &piece, QcBlockId foundation) const;
    // Levels the ground of the doorsteps that lie in the chunk to their height: the terrain above
    // cleared, the foundation filled in below (decision E-409).
    void buildThresholds(QcChunk &chunk, const QList<QcBlockPos> &thresholds, QcBlockId foundation) const;
    // Lays the road cells that lie in the chunk at their height: into the ground's top block, onto it
    // where the cell lies one higher, the terrain above cleared.
    void buildRoads(QcChunk &chunk, const QList<QcBlockPos> &cells, QcBlockId path) const;
    // Lays the steps of the roads (slabs where a road rises by one block) that lie in the chunk.
    void buildSteps(QcChunk &chunk, const QList<QcVillagePlan::Step> &steps) const;
    // Lays the jetty cells that lie in the chunk, with the posts under them down to the ground.
    void buildDecks(QcChunk &chunk, const QList<QcVillagePlan::Deck> &decks) const;
    // Solid ground a post, foundation or road can stand on; not frozen water (decision E-815).
    bool isGround(QcBlockId block) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QcVillageStyles m_styles;
    QcVillagePlanner m_planner;
    QcWetlandShaper m_wetland;
    // The ground around a centre with blended ground (decision E-893).
    QcSurfaceBlend m_centreBlend;
    QcVillagePlanCache m_cache{cachedPlans};
    // The biomes' frozen water blocks (ice), which isGround() does not count as ground.
    QList<QcBlockId> m_frozenWater;
};
