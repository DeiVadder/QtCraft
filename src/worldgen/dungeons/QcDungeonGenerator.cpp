#include "worldgen/dungeons/QcDungeonGenerator.h"

#include "worldgen/terrain/QcNoise.h"

#include <QSet>

#include <array>
#include <cmath>

namespace {

// Regions whose plans are kept at most; beyond, the cache starts over.
constexpr qsizetype cacheCapacity = 4096;

int floorDiv(int value, int divisor)
{
    return static_cast<int>(std::floor(static_cast<double>(value) / divisor));
}

} // namespace

QcDungeonGenerator::QcDungeonGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_planner(terrain, content, seed)
    , m_seed(qcMixSeed(seed, 0x3802))
    , m_lootTable(content.worldgen().terrainSettings().dungeons.lootTable)
{
    m_rock.resize(static_cast<std::size_t>(content.stateCount()));
    m_liquid.resize(m_rock.size());
    for (std::size_t id = 0; id < m_rock.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        m_rock[id] = block.solid && block.opaque && block.tool == QStringLiteral("pickaxe");
        m_liquid[id] = block.liquid;
    }
}

std::shared_ptr<const QcDungeonPlan> QcDungeonGenerator::planOf(int regionX, int regionZ) const
{
    const qint64 key = (static_cast<qint64>(regionX) << 32) ^ static_cast<qint64>(static_cast<quint32>(regionZ));
    {
        const QMutexLocker lock(&m_mutex);
        if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
            return *it;
        }
    }
    const std::shared_ptr<const QcDungeonPlan> plan = m_planner.plan(regionX, regionZ);
    const QMutexLocker lock(&m_mutex);
    if (m_plans.size() >= cacheCapacity) {
        m_plans.clear();
    }
    return *m_plans.insert(key, plan);
}

QList<std::shared_ptr<const QcDungeonPlan>> QcDungeonGenerator::plansTouching(QcChunkPos pos) const
{
    QList<std::shared_ptr<const QcDungeonPlan>> plans;
    if (!m_planner.enabled()) {
        return plans;
    }
    const int region = m_planner.settings().regionSize;
    const int x0 = pos.x * QcWorld::chunkSize;
    const int z0 = pos.z * QcWorld::chunkSize;
    const int x1 = x0 + QcWorld::chunkSize - 1;
    const int z1 = z0 + QcWorld::chunkSize - 1;
    for (int rz = floorDiv(z0 - m_planner.reach(), region); rz <= floorDiv(z1 + m_planner.reach(), region); ++rz) {
        for (int rx = floorDiv(x0 - m_planner.reach(), region); rx <= floorDiv(x1 + m_planner.reach(), region); ++rx) {
            std::shared_ptr<const QcDungeonPlan> plan = planOf(rx, rz);
            if (plan && plan->touches(x0, z0, x1, z1)) {
                plans.append(std::move(plan));
            }
        }
    }
    return plans;
}

void QcDungeonGenerator::build(QcChunk &chunk) const
{
    const QList<std::shared_ptr<const QcDungeonPlan>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcDungeonPlan> &plan : plans) {
        build(chunk, *plan);
    }
}

QcBlockId QcDungeonGenerator::wallAt(QcBlockPos pos, bool floor) const
{
    const double moss = m_planner.settings().mossChance * (floor ? 2.0 : 1.0);
    return qcPositionRandom(m_seed, pos.x, pos.y, pos.z) < moss ? m_planner.blocks().moss : m_planner.blocks().wall;
}

void QcDungeonGenerator::build(QcChunk &chunk, const QcDungeonPlan &plan) const
{
    const QcChunkPos pos = chunk.pos();
    const auto inChunk = [pos](QcBlockPos cell) { return cell.chunk() == pos && cell.isInsideWorldHeight(); };
    const auto set = [&](QcBlockPos cell, QcBlockId block) {
        if (inChunk(cell)) {
            chunk.setBlock(cell.localX(), cell.y, cell.localZ(), block);
        }
    };

    // The room: a shell around air.
    for (int dy = 0; dy < plan.sizeY; ++dy) {
        for (int dz = 0; dz < plan.sizeZ; ++dz) {
            for (int dx = 0; dx < plan.sizeX; ++dx) {
                const QcBlockPos cell = plan.origin.offset(dx, dy, dz);
                const bool shell = dy == 0 || dy == plan.sizeY - 1 || dx == 0 || dz == 0 || dx == plan.sizeX - 1 || dz == plan.sizeZ - 1;
                set(cell, shell ? wallAt(cell, dy == 0) : qcAirBlockId);
            }
        }
    }

    // The corridors: lined where they border no rock, then carved.
    const int floorY = plan.origin.y;
    for (const QcBlockPos &cell : plan.lining) {
        if (inChunk(cell) && !m_rock[static_cast<std::size_t>(chunk.block(cell.localX(), cell.y, cell.localZ()))]) {
            set(cell, wallAt(cell, cell.y <= floorY));
        }
    }
    for (const QcDungeonPlan::Corridor &corridor : plan.corridors) {
        for (const QcBlockPos &feet : corridor.path) {
            set(feet, qcAirBlockId);
            set(feet.offset(0, 1, 0), qcAirBlockId);
        }
    }
    sealLiquids(chunk, plan);

    set(plan.spawner, m_planner.blocks().spawner);
    for (const QcDungeonPlan::Placed &placed : plan.chests) {
        set(placed.at, placed.block);
    }
    for (const QcDungeonPlan::Placed &placed : plan.lights) {
        set(placed.at, placed.block);
    }
}

void QcDungeonGenerator::sealLiquids(QcChunk &chunk, const QcDungeonPlan &plan) const
{
    QSet<QcBlockPos> carved;
    for (const QcDungeonPlan::Corridor &corridor : plan.corridors) {
        for (const QcBlockPos &feet : corridor.path) {
            carved.insert(feet);
            carved.insert(feet.offset(0, 1, 0));
        }
    }
    // Liquid of this chunk next to a carved cell (a pool where a corridor reaches a cave) becomes
    // wall: generated liquids stay still (it would flow into the corridor).
    constexpr std::array<std::array<int, 3>, 5> around{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}}};
    for (const QcBlockPos &cell : std::as_const(carved)) {
        for (const auto &[dx, dy, dz] : around) {
            const QcBlockPos next = cell.offset(dx, dy, dz);
            if (next.chunk() != chunk.pos() || !next.isInsideWorldHeight() || carved.contains(next) || plan.inRoom(next)) {
                continue;
            }
            if (m_liquid[static_cast<std::size_t>(chunk.block(next.localX(), next.y, next.localZ()))]) {
                chunk.setBlock(next.localX(), next.y, next.localZ(), wallAt(next, false));
            }
        }
    }
}

QcChunkFurnishing QcDungeonGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    if (!m_planner.enabled()) {
        return furnishing;
    }

    // The room, and with it the spawner and the chests, lies inside its region (QcDungeonPlanner
    // keeps it off the region's edges): only the regions the chunk overlaps can furnish it.
    const int region = m_planner.settings().regionSize;
    const int x0 = pos.x * QcWorld::chunkSize;
    const int z0 = pos.z * QcWorld::chunkSize;
    QList<std::shared_ptr<const QcDungeonPlan>> plans;
    for (int rz = floorDiv(z0, region); rz <= floorDiv(z0 + QcWorld::chunkSize - 1, region); ++rz) {
        for (int rx = floorDiv(x0, region); rx <= floorDiv(x0 + QcWorld::chunkSize - 1, region); ++rx) {
            if (std::shared_ptr<const QcDungeonPlan> plan = planOf(rx, rz)) {
                plans.append(std::move(plan));
            }
        }
    }
    for (const std::shared_ptr<const QcDungeonPlan> &plan : std::as_const(plans)) {
        if (plan->spawner.chunk() == pos) {
            furnishing.spawners.append({plan->spawner, plan->mob});
        }
        for (const QcDungeonPlan::Placed &chest : plan->chests) {
            if (chest.at.chunk() == pos) {
                const quint64 seed = qcMixSeed(m_seed,
                    static_cast<quint64>(qcPositionRandom(m_seed ^ 0x5eed, chest.at.x, chest.at.y, chest.at.z) * 4294967296.0));
                furnishing.chests.append({chest.at, m_lootTable, seed});
            }
        }
    }
    return furnishing;
}
