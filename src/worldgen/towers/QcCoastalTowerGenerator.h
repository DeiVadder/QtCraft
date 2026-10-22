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
    // The region's tower, planned once (null without one): the planner's, unless a neighbouring
    // region's of higher priority stands closer than `minDistance` (decision E-857).
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
    // The planner's tower of a region before the spacing, planned once.
    [[nodiscard]] std::shared_ptr<const QcCoastalTowerPlan> plannedOf(int regionX, int regionZ) const;
    // Whether a neighbouring region's tower of higher priority comes closer than `minDistance`.
    bool crowded(const QcCoastalTowerPlan &plan) const;
    // The squared distance from a point to the nearest column of a region, both in doubled world
    // coordinates.
    [[nodiscard]] qint64 regionDistanceSquared(int regionX, int regionZ, int doubledX, int doubledZ) const;
    // The towers whose footprint lies within `margin` columns of the chunk.
    QList<std::shared_ptr<const QcCoastalTowerPlan>> plansNear(QcChunkPos pos, int margin) const;
    void build(QcChunk &chunk, const QcCoastalTowerPlan &plan) const;
    // Clears the column above the floor and fills it below the floor down to the ground.
    void prepareColumn(QcChunk &chunk, const QcCoastalTowerPlan &plan, int x, int z) const;

    QcCoastalTowerPlanner m_planner;
    quint64 m_seed = 0;
    // Per block state: ground a foundation stands on (solid, opaque, no liquid).
    std::vector<bool> m_ground;
    // Plans of regions by key, each made once outside the lock and kept up to a capacity (then the
    // cache starts over); thread-safe.
    class PlanCache
    {
    public:
        using Plan = std::shared_ptr<const QcCoastalTowerPlan>;

        template<typename Make>
        Plan get(qint64 key, Make make)
        {
            {
                const QMutexLocker lock(&m_mutex);
                if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
                    return *it;
                }
            }
            const Plan plan = make();
            const QMutexLocker lock(&m_mutex);
            if (m_plans.size() >= capacity) {
                m_plans.clear();
            }
            return *m_plans.insert(key, plan);
        }

    private:
        static constexpr qsizetype capacity = 1024;
        QMutex m_mutex;
        QHash<qint64, Plan> m_plans;
    };

    // Plans after the spacing rule, and before it (what the neighbours compare).
    mutable PlanCache m_plans;
    mutable PlanCache m_planned;
};
