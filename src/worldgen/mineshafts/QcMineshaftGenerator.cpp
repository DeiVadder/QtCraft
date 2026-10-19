#include "worldgen/mineshafts/QcMineshaftGenerator.h"

#include "worldgen/terrain/QcNoise.h"

#include <cmath>

namespace {

// Regions whose plans are kept at most; beyond, the cache starts over.
constexpr qsizetype cacheCapacity = 1024;

int floorDiv(int value, int divisor)
{
    return int(std::floor(double(value) / divisor));
}

} // namespace

QcMineshaftGenerator::QcMineshaftGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_planner(terrain, content, seed)
    , m_builder(m_planner.blocks(), m_planner.settings().maxPillar)
    , m_seed(qcMixSeed(seed, 0x3804))
{
}

std::shared_ptr<const QcMineshaftPlan> QcMineshaftGenerator::planOf(int regionX, int regionZ) const
{
    const qint64 key = (qint64(regionX) << 32) ^ qint64(quint32(regionZ));
    {
        const QMutexLocker lock(&m_mutex);
        if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
            return *it;
        }
    }
    const std::shared_ptr<const QcMineshaftPlan> plan = m_planner.plan(regionX, regionZ);
    const QMutexLocker lock(&m_mutex);
    if (m_plans.size() >= cacheCapacity) {
        m_plans.clear();
    }
    return *m_plans.insert(key, plan);
}

QList<std::shared_ptr<const QcMineshaftPlan>> QcMineshaftGenerator::plansTouching(QcChunkPos pos) const
{
    QList<std::shared_ptr<const QcMineshaftPlan>> plans;
    if (!m_planner.enabled()) {
        return plans;
    }
    const int region = m_planner.settings().regionSize;
    const int x0 = pos.x * QcWorld::chunkSize;
    const int z0 = pos.z * QcWorld::chunkSize;
    const int x1 = x0 + QcWorld::chunkSize - 1;
    const int z1 = z0 + QcWorld::chunkSize - 1;
    for (int rz = floorDiv(z0, region); rz <= floorDiv(z1, region); ++rz) {
        for (int rx = floorDiv(x0, region); rx <= floorDiv(x1, region); ++rx) {
            std::shared_ptr<const QcMineshaftPlan> plan = planOf(rx, rz);
            if (plan && plan->touches(x0, z0, x1, z1)) {
                plans.append(std::move(plan));
            }
        }
    }
    return plans;
}

void QcMineshaftGenerator::build(QcChunk &chunk) const
{
    const QList<std::shared_ptr<const QcMineshaftPlan>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcMineshaftPlan> &plan : plans) {
        m_builder.build(chunk, *plan);
    }
}

void QcMineshaftGenerator::buildEntrances(QcChunk &chunk) const
{
    const QList<std::shared_ptr<const QcMineshaftPlan>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcMineshaftPlan> &plan : plans) {
        if (plan->entrance) {
            m_builder.buildEntrance(chunk, *plan->entrance);
        }
    }
}

quint64 QcMineshaftGenerator::lootSeed(QcBlockPos at) const
{
    return qcMixSeed(m_seed, quint64(qcPositionRandom(m_seed ^ 0x5eed, at.x, at.y, at.z) * 4294967296.0));
}

QcChunkFurnishing QcMineshaftGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QcMineshaftSettings &settings = m_planner.settings();
    const QList<std::shared_ptr<const QcMineshaftPlan>> plans = plansTouching(pos);
    for (const std::shared_ptr<const QcMineshaftPlan> &plan : plans) {
        for (const QcMineshaftPlan::Spawner &spawner : plan->spawners) {
            if (spawner.at.chunk() == pos) {
                furnishing.spawners.append({spawner.at, settings.mob});
            }
        }
        for (const QcBlockPos &cart : plan->carts) {
            if (cart.chunk() == pos) {
                furnishing.carts.append({cart, settings.cart, settings.lootTable, lootSeed(cart)});
            }
        }
        for (const QcMineshaftPlan::Room &room : plan->rooms) {
            if (room.placement.overlaps(pos)) {
                furnishing.add(roomFurnishing(pos, room, lootSeed(room.placement.origin())));
            }
        }
    }
    return furnishing;
}

QcChunkFurnishing QcMineshaftGenerator::roomFurnishing(QcChunkPos pos, const QcMineshaftPlan::Room &room, quint64 seed)
{
    QcChunkFurnishing furnishing;
    const QcStructurePlacement &placement = room.placement;
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
    for (const QcStructureDefinition::Cart &cart : placement.structure().minecarts) {
        if (const QcBlockPos at = placement.toWorld(cart.at); at.chunk() == pos) {
            furnishing.carts.append({at, cart.item, {}, 0});
        }
    }
    return furnishing;
}
