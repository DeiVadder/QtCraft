#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcStructureSites.h"
#include "worldgen/surface/QcSurfacePlanner.h"

#include <QHash>
#include <QMutex>

#include <memory>
#include <optional>

class QcVillageGenerator;

// The structures the terrain generator places by itself on the surface (decision E-590, the desert
// temple): each kind plans its regions once (QcSurfacePlanner, plans cached), a copy lies wholly in
// its region, so a chunk asks only the one region of each kind it lies in. Writing a copy into a
// chunk clears the terrain above its footprint (from its layer `clearFrom` to `clearance` blocks over
// its top; over water never at or below the sea, decision E-622; not for kinds without
// `clearTerrain`, decision E-630), fills the ground below it with the kind's foundation down to
// solid ground and writes its cells; the same in any order of the chunks. No trees, plants or
// surface pools grow near a copy (footprintsNear()), so none is cut or left floating. Its loot
// comes with furnishingIn(), its guardian zones with sitesNear().
// Thread-safe.
class QcSurfaceStructures
{
public:
    // Blocks cleared above a copy, and the deepest foundation below it.
    static constexpr int clearance = 4;
    static constexpr int maxFoundation = 16;
    // Regions whose plans are kept at most; beyond, the cache starts over.
    static constexpr qsizetype cacheCapacity = 4096;

    QcSurfaceStructures(const QcTerrainGenerator &terrain, const QcVillageGenerator &villages, const QcContentRegistry &content, quint64 seed);

    const QcSurfacePlanner &planner() const { return m_planner; }
    // The copy of kind `kind` in the region, planned once.
    [[nodiscard]] std::shared_ptr<const QcStructurePlacement> planOf(int kind, int regionX, int regionZ) const;
    // The copies of every kind whose footprint overlaps the chunk.
    QList<std::shared_ptr<const QcStructurePlacement>> plansTouching(QcChunkPos pos) const;
    // The footprints of the copies of every kind within `margin` columns of the chunk: the terrain
    // generator places no features and no surface pools there (decision E-630).
    QList<QcSurfacePlanner::Rect> footprintsNear(QcChunkPos pos, int margin) const;
    void build(QcChunk &chunk) const;
    // The loot of the copies' chests in the chunk.
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;
    // The guardian zones and spawn areas of the copies in the regions within `reach` of `from`.
    QcStructureSites sitesNear(QcBlockPos from, int reach) const;
    // The same from the regions planned so far, never planning (the server's tick thread);
    // std::nullopt when a region within `reach` is not planned yet.
    [[nodiscard]] std::optional<QcStructureSites> plannedSitesNear(QcBlockPos from, int reach) const;
    // The copy of kind `name` nearest to `from` within `reach` blocks, if any.
    [[nodiscard]] std::optional<QcStructurePlacement> nearest(const QString &name, QcBlockPos from, int reach) const;

private:
    // The copy of kind `kind` in the region when the region is planned; std::nullopt otherwise.
    [[nodiscard]] std::optional<std::shared_ptr<const QcStructurePlacement>> plannedOf(int kind, int regionX, int regionZ) const;
    // The sites of the regions within `reach` of `from`; with `mayPlan` false only from
    // planned regions (std::nullopt when one is not planned yet).
    [[nodiscard]] std::optional<QcStructureSites> collectSites(QcBlockPos from, int reach, bool mayPlan) const;
    // Whether a village or a copy of a kind before `kind` lies within `margin` of the footprint.
    bool occupied(int kind, QcSurfacePlanner::Rect footprint, int margin) const;
    void build(QcChunk &chunk, const QcStructurePlacement &placement, const QcSurfacePlanner::Kind &kind) const;

    const QcVillageGenerator &m_villages;
    QcSurfacePlanner m_planner;
    // Per block state: ground a foundation stops at (solid and not a liquid).
    std::vector<bool> m_ground;
    mutable QMutex m_mutex;
    // By kind and region (QcSurfaceStructures.cpp, planKey).
    mutable QHash<quint64, std::shared_ptr<const QcStructurePlacement>> m_plans;
};
