#include "worldgen/towers/QcCoastalTowerGenerator.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <cmath>

namespace {

// Blocks cleared above the structure's top layer (tree tops over the roof).
constexpr int clearAbove = 4;
// Cells above the floor kept open in front of the door.
constexpr int doorHeight = 3;

int floorDiv(int value, int divisor)
{
    return static_cast<int>(std::floor(static_cast<double>(value) / divisor));
}

} // namespace

QcCoastalTowerGenerator::QcCoastalTowerGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_planner(terrain, content, seed)
    , m_seed(qcMixSeed(seed, 0x5102))
{
    m_ground.resize(static_cast<std::size_t>(content.stateCount()));
    for (std::size_t id = 0; id < m_ground.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        m_ground[id] = block.solid && block.opaque && !block.liquid;
    }
}

namespace {

qint64 squared(int value)
{
    return static_cast<qint64>(value) * value;
}

qint64 regionKey(int regionX, int regionZ)
{
    return (static_cast<qint64>(regionX) << 32) ^ static_cast<qint64>(static_cast<quint32>(regionZ));
}

} // namespace

std::shared_ptr<const QcCoastalTowerPlan> QcCoastalTowerGenerator::plannedOf(int regionX, int regionZ) const
{
    return m_planned.get(regionKey(regionX, regionZ), [&] { return m_planner.plan(regionX, regionZ); });
}

std::shared_ptr<const QcCoastalTowerPlan> QcCoastalTowerGenerator::planOf(int regionX, int regionZ) const
{
    return m_plans.get(regionKey(regionX, regionZ), [&]() -> std::shared_ptr<const QcCoastalTowerPlan> {
        const std::shared_ptr<const QcCoastalTowerPlan> plan = plannedOf(regionX, regionZ);
        return plan && crowded(*plan) ? nullptr : plan;
    });
}

bool QcCoastalTowerGenerator::crowded(const QcCoastalTowerPlan &plan) const
{
    // Towers lie inside their regions, and a region is at least as wide as the distance: only the
    // eight neighbours can come closer. Their plans before the spacing decide, so no region waits
    // on another's spacing and the rule is the same from every side.
    const int minDistance = m_planner.settings().minDistance;
    if (minDistance <= 0) {
        return false;
    }
    const auto centre = [](const QcCoastalTowerPlan &tower) {
        const QcCoastalTowerPlan::Footprint footprint = tower.footprint();
        return std::pair{footprint.minX + footprint.maxX, footprint.minZ + footprint.maxZ};
    };
    const auto [x, z] = centre(plan);
    const double own = m_planner.priority(plan.regionX, plan.regionZ);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int regionX = plan.regionX + dx;
            const int regionZ = plan.regionZ + dz;
            // Only a neighbour of higher priority whose region comes near the tower can crowd it:
            // the others are never planned for this.
            // Squared distances in doubled coordinates, compared with the doubled distance squared.
            const qint64 limit = 4 * static_cast<qint64>(minDistance) * minDistance;
            if ((dx == 0 && dz == 0) || m_planner.priority(regionX, regionZ) <= own || regionDistanceSquared(regionX, regionZ, x, z) >= limit) {
                continue;
            }
            const std::shared_ptr<const QcCoastalTowerPlan> other = plannedOf(regionX, regionZ);
            if (other && squared(centre(*other).first - x) + squared(centre(*other).second - z) < limit) {
                return true;
            }
        }
    }
    return false;
}

qint64 QcCoastalTowerGenerator::regionDistanceSquared(int regionX, int regionZ, int doubledX, int doubledZ) const
{
    // The region's columns in doubled coordinates.
    const int size = m_planner.settings().regionSize;
    const int nearestX = std::clamp(doubledX, 2 * regionX * size, 2 * (regionX * size + size - 1));
    const int nearestZ = std::clamp(doubledZ, 2 * regionZ * size, 2 * (regionZ * size + size - 1));
    return squared(nearestX - doubledX) + squared(nearestZ - doubledZ);
}

QList<std::shared_ptr<const QcCoastalTowerPlan>> QcCoastalTowerGenerator::plansTouching(QcChunkPos pos) const
{
    // The doorstep lies one column outside the footprint.
    return plansNear(pos, 1);
}

QList<QcCoastalTowerPlan::Footprint> QcCoastalTowerGenerator::footprintsNear(QcChunkPos pos, int margin) const
{
    QList<QcCoastalTowerPlan::Footprint> footprints;
    const QList<std::shared_ptr<const QcCoastalTowerPlan>> plans = plansNear(pos, margin);
    for (const std::shared_ptr<const QcCoastalTowerPlan> &plan : plans) {
        footprints.append(plan->footprint());
    }
    return footprints;
}

QList<std::shared_ptr<const QcCoastalTowerPlan>> QcCoastalTowerGenerator::plansNear(QcChunkPos pos, int margin) const
{
    QList<std::shared_ptr<const QcCoastalTowerPlan>> plans;
    if (!m_planner.enabled()) {
        return plans;
    }
    const int region = m_planner.settings().regionSize;
    const int x0 = pos.x * QcWorld::chunkSize;
    const int z0 = pos.z * QcWorld::chunkSize;
    const QcCoastalTowerPlan::Footprint chunk{x0, z0, x0 + QcWorld::chunkSize - 1, z0 + QcWorld::chunkSize - 1};
    const QcCoastalTowerPlan::Footprint around = chunk.grown(margin);
    for (int rz = floorDiv(around.minZ, region); rz <= floorDiv(around.maxZ, region); ++rz) {
        for (int rx = floorDiv(around.minX, region); rx <= floorDiv(around.maxX, region); ++rx) {
            std::shared_ptr<const QcCoastalTowerPlan> plan = planOf(rx, rz);
            if (plan && plan->footprint().grown(margin).touches(chunk)) {
                plans.append(std::move(plan));
            }
        }
    }
    return plans;
}

void QcCoastalTowerGenerator::build(QcChunk &chunk) const
{
    const QList<std::shared_ptr<const QcCoastalTowerPlan>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcCoastalTowerPlan> &plan : plans) {
        build(chunk, *plan);
    }
}

void QcCoastalTowerGenerator::build(QcChunk &chunk, const QcCoastalTowerPlan &plan) const
{
    const QcCoastalTowerPlan::Footprint footprint = plan.footprint();
    const int x0 = chunk.pos().x * QcWorld::chunkSize;
    const int z0 = chunk.pos().z * QcWorld::chunkSize;
    for (int z = std::max(z0, footprint.minZ); z <= std::min(z0 + QcWorld::chunkSize - 1, footprint.maxZ); ++z) {
        for (int x = std::max(x0, footprint.minX); x <= std::min(x0 + QcWorld::chunkSize - 1, footprint.maxX); ++x) {
            prepareColumn(chunk, plan, x, z);
        }
    }
    plan.placement.writeInto(chunk);

    // The way to the door stays open (a tree may stand there).
    if (plan.doorstep.chunk() == chunk.pos()) {
        for (int y = plan.doorstep.y + 1; y <= plan.placement.origin().y + doorHeight; ++y) {
            chunk.setBlock(plan.doorstep.localX(), y, plan.doorstep.localZ(), qcAirBlockId);
        }
    }
}

void QcCoastalTowerGenerator::prepareColumn(QcChunk &chunk, const QcCoastalTowerPlan &plan, int x, int z) const
{
    const QcBlockPos at{x, 0, z};
    const int localX = at.localX();
    const int localZ = at.localZ();
    const int floor = plan.placement.origin().y;

    const int top = std::min(QcWorld::worldHeight - 1, floor + plan.placement.structure().sizeY() - 1 + clearAbove);
    for (int y = floor + 1; y <= top; ++y) {
        chunk.setBlock(localX, y, localZ, qcAirBlockId);
    }

    const int bottom = std::max(0, floor - m_planner.settings().maxFoundation);
    for (int y = floor - 1; y >= bottom; --y) {
        if (m_ground[static_cast<std::size_t>(chunk.block(localX, y, localZ))]) {
            break;
        }
        chunk.setBlock(localX, y, localZ, m_planner.foundation());
    }
}

QcChunkFurnishing QcCoastalTowerGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QList<std::shared_ptr<const QcCoastalTowerPlan>> plans = plansTouching(pos);
    for (const std::shared_ptr<const QcCoastalTowerPlan> &plan : plans) {
        const QcStructurePlacement &placement = plan->placement;
        const quint64 seed = qcMixSeed(m_seed, static_cast<quint64>(qcPositionRandom(m_seed ^ 0x5eed, plan->regionX, 0, plan->regionZ) * 4294967296.0));
        for (const QcStructureDefinition::Loot &loot : placement.structure().loot) {
            if (const QcBlockPos at = placement.toWorld(loot.at); at.chunk() == pos) {
                furnishing.chests.append({at, loot.table, qcMixSeed(seed, static_cast<quint64>(qcPositionRandom(seed, at.x, at.y, at.z) * 4294967296.0))});
            }
        }
        for (const QcStructureDefinition::Spawner &spawner : placement.structure().spawners) {
            if (const QcBlockPos at = placement.toWorld(spawner.at); at.chunk() == pos) {
                furnishing.spawners.append({at, spawner.mob, spawner.confined});
            }
        }
    }
    return furnishing;
}
