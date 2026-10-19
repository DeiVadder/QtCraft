#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/towers/QcCoastalTowerPlanner.h"

#include <QHash>
#include <QMutex>

#include <memory>
#include <vector>

// Builds the coastal towers of the terrain generator into its chunks (decision E-605): each chunk
// writes its own cells of the tower of every region it overlaps (a tower stays inside its region;
// QcCoastalTowerPlanner, plans cached by region), in any order with the same result. Under the
// footprint the foundation block fills everything that is not ground from below the floor down to
// the ground (at most `maxFoundation` blocks); above the floor the footprint is cleared (trees,
// plants) up to a few blocks over the structure, which is then written; no trees, plants or surface
// pools grow near a tower (footprintsNear()), so none is cut or left floating. The chests' loot and
// spawners of the structure come with furnishingIn(). Thread-safe.
class QcCoastalTowerGenerator
{
public:
    QcCoastalTowerGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    const QcCoastalTowerPlanner &planner() const { return m_planner; }
    // The region's plan, planned once (null without a tower).
    [[nodiscard]] std::shared_ptr<const QcCoastalTowerPlan> planOf(int regionX, int regionZ) const;
    // The towers whose cells may lie in the chunk.
    QList<std::shared_ptr<const QcCoastalTowerPlan>> plansTouching(QcChunkPos pos) const;
    // The footprints of the towers within `margin` columns of the chunk: the terrain generator
    // places no features and no surface pools there, like near surface structures.
    QList<QcCoastalTowerPlan::Footprint> footprintsNear(QcChunkPos pos, int margin) const;
    // Builds after the terrain's features and surface pools, before the villages.
    void build(QcChunk &chunk) const;
    // The chunk's chests and spawners of the towers' structures.
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;

private:
    // The towers whose footprint lies within `margin` columns of the chunk.
    QList<std::shared_ptr<const QcCoastalTowerPlan>> plansNear(QcChunkPos pos, int margin) const;
    void build(QcChunk &chunk, const QcCoastalTowerPlan &plan) const;
    // Clears the column above the floor and fills it below the floor down to the ground.
    void prepareColumn(QcChunk &chunk, const QcCoastalTowerPlan &plan, int x, int z) const;

    QcCoastalTowerPlanner m_planner;
    quint64 m_seed = 0;
    // Per block state: ground a foundation stands on (solid, opaque, no liquid).
    std::vector<bool> m_ground;
    mutable QMutex m_mutex;
    mutable QHash<qint64, std::shared_ptr<const QcCoastalTowerPlan>> m_plans;
};
