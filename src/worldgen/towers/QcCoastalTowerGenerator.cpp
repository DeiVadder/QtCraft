#include "worldgen/towers/QcCoastalTowerGenerator.h"

#include "worldgen/terrain/QcNoise.h"

#include <cmath>

namespace {

// Regions whose plans are kept at most; beyond, the cache starts over.
constexpr qsizetype cacheCapacity = 1024;
// Blocks cleared above the structure's top layer (tree tops over the roof).
constexpr int clearAbove = 4;
// Cells above the floor kept open in front of the door.
constexpr int doorHeight = 3;

int floorDiv(int value, int divisor)
{
    return int(std::floor(double(value) / divisor));
}

} // namespace

QcCoastalTowerGenerator::QcCoastalTowerGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed,
                                                 QStringList *warnings)
    : m_planner(terrain, content, seed, warnings)
    , m_seed(qcMixSeed(seed, 0x5102))
{
    m_ground.resize(std::size_t(content.stateCount()));
    for (std::size_t id = 0; id < m_ground.size(); ++id) {
        const QcBlockDefinition &block = content.block(QcBlockId(id));
        m_ground[id] = block.solid && block.opaque && !block.liquid;
    }
}

std::shared_ptr<const QcCoastalTowerPlan> QcCoastalTowerGenerator::planOf(int regionX, int regionZ) const
{
    const qint64 key = (qint64(regionX) << 32) ^ qint64(quint32(regionZ));
    {
        const QMutexLocker lock(&m_mutex);
        if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
            return *it;
        }
    }
    const std::shared_ptr<const QcCoastalTowerPlan> plan = m_planner.plan(regionX, regionZ);
    const QMutexLocker lock(&m_mutex);
    if (m_plans.size() >= cacheCapacity) {
        m_plans.clear();
    }
    return *m_plans.insert(key, plan);
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
        if (m_ground[std::size_t(chunk.block(localX, y, localZ))]) {
            break;
        }
        chunk.setBlock(localX, y, localZ, m_planner.foundation());
    }
}

void QcCoastalTowerGenerator::addFurnishing(QcChunkPos pos, QcChunkFurnishing &furnishing) const
{
    const QList<std::shared_ptr<const QcCoastalTowerPlan>> plans = plansTouching(pos);
    for (const std::shared_ptr<const QcCoastalTowerPlan> &plan : plans) {
        const QcStructurePlacement &placement = plan->placement;
        const quint64 seed = qcMixSeed(m_seed, quint64(qcPositionRandom(m_seed ^ 0x5eed, plan->regionX, 0, plan->regionZ) * 4294967296.0));
        for (const QcStructureDefinition::Loot &loot : placement.structure().loot) {
            if (const QcBlockPos at = placement.toWorld(loot.at); at.chunk() == pos) {
                furnishing.chests.append({at, loot.table, qcMixSeed(seed, quint64(qcPositionRandom(seed, at.x, at.y, at.z) * 4294967296.0))});
            }
        }
        for (const QcStructureDefinition::Spawner &spawner : placement.structure().spawners) {
            if (const QcBlockPos at = placement.toWorld(spawner.at); at.chunk() == pos) {
                furnishing.spawners.append({at, spawner.mob, spawner.confined});
            }
        }
    }
}
