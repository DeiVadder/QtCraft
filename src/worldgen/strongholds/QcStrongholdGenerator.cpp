// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdGenerator.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>

namespace {

// The widest piece beyond the reach of its doorway, and a cell round it.
constexpr int pieceMargin = 18;

// Whether `terrain` lets a stronghold stand on the column x, z: land, no sea, river or delta.
bool isLand(const QcTerrainGenerator &terrain, int x, int z)
{
    const QcTerrainColumn column = terrain.column(x, z);
    return column.height >= terrain.seaLevel() && !terrain.isRiverBiome(column.biome) && !terrain.isDeltaBiome(column.biome);
}

} // namespace

QcStrongholdGenerator::QcStrongholdGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_terrain(terrain)
    , m_rings(seed, [&terrain](int x, int z) { return isLand(terrain, x, z); })
    , m_planner(content.worldgen().terrainSettings().strongholds, [&terrain](int x, int z) { return terrain.column(x, z).height; }, seed)
    , m_lootSeed(qcMixSeed(seed, 0x5c4e))
{
    if (!planner().settings().enabled) {
        return;
    }
    const QcStrongholdBlocks::Resolved resolved = QcStrongholdBlocks::resolve(planner().settings(), content);
    m_warnings = resolved.warnings;
    m_blocks = resolved.blocks;
    if (m_blocks) {
        m_builder.emplace(*m_blocks, planner().settings(), qcMixSeed(seed, 0xb71d));
    }
}

int QcStrongholdGenerator::extent() const
{
    return planner().settings().reach + pieceMargin;
}

std::optional<std::shared_ptr<const QcStrongholdPlan>> QcStrongholdGenerator::cached(int index) const
{
    const std::scoped_lock lock(m_mutex);
    const auto it = m_plans.constFind(index);
    return it == m_plans.cend() ? std::nullopt : std::optional(*it);
}

std::shared_ptr<const QcStrongholdPlan> QcStrongholdGenerator::planOf(int index) const
{
    if (!enabled()) {
        return nullptr;
    }
    if (std::optional<std::shared_ptr<const QcStrongholdPlan>> plan = cached(index)) {
        return *std::move(plan);
    }
    // Planned outside the lock (it reads the terrain); two threads at once make the same plan.
    std::shared_ptr<const QcStrongholdPlan> plan = planner().plan(index, rings().positionOf(index));
    const std::scoped_lock lock(m_mutex);
    return *m_plans.insert(index, std::move(plan));
}

QList<std::shared_ptr<const QcStrongholdPlan>> QcStrongholdGenerator::plansIn(QcXzRect area) const
{
    QList<std::shared_ptr<const QcStrongholdPlan>> plans;
    if (!enabled()) {
        return plans;
    }
    const QList<int> indices = rings().near(area, extent());
    for (const int index : indices) {
        std::shared_ptr<const QcStrongholdPlan> plan = planOf(index);
        if (plan->touches(area) && m_terrain.buildsStructure(plan->bounds)) {
            plans.append(std::move(plan));
        }
    }
    return plans;
}

void QcStrongholdGenerator::build(QcChunk &chunk) const
{
    const QList<std::shared_ptr<const QcStrongholdPlan>> plans = plansIn(QcXzRect::ofChunk(chunk.pos()));
    for (const std::shared_ptr<const QcStrongholdPlan> &plan : plans) {
        builder().build(chunk, *plan);
    }
}

QString QcStrongholdGenerator::lootTable(QcStrongholdPlan::Loot loot) const
{
    switch (loot) {
    case QcStrongholdPlan::Loot::Store:
        return planner().settings().storeLoot;
    case QcStrongholdPlan::Loot::Library:
        return planner().settings().libraryLoot;
    case QcStrongholdPlan::Loot::Corridor:
        break;
    }
    return planner().settings().corridorLoot;
}

QcChunkFurnishing QcStrongholdGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QList<std::shared_ptr<const QcStrongholdPlan>> plans = plansIn(QcXzRect::ofChunk(pos));
    for (const std::shared_ptr<const QcStrongholdPlan> &plan : plans) {
        for (const QcStrongholdPlan::Chest &chest : plan->chests) {
            if (chest.at.chunk() == pos) {
                furnishing.chests.append({chest.at, lootTable(chest.loot), QcChunkFurnishing::chestSeed(m_lootSeed, chest.at)});
            }
        }
        for (const QcBlockPos &spawner : plan->spawners) {
            if (spawner.chunk() == pos) {
                furnishing.spawners.append({spawner, planner().settings().mob});
            }
        }
    }
    return furnishing;
}

std::optional<bool> QcStrongholdGenerator::plannedHolds(QcBlockPos at) const
{
    if (!enabled()) {
        return false;
    }
    const std::optional<QList<int>> indices = rings().knownNear({at.x, at.z, at.x, at.z}, extent());
    if (!indices) {
        return std::nullopt;
    }
    bool holds = false;
    for (const int index : *indices) {
        const std::optional<std::shared_ptr<const QcStrongholdPlan>> plan = cached(index);
        if (!plan) {
            return std::nullopt;
        }
        holds = holds || ((*plan)->holds(at) && m_terrain.buildsStructure((*plan)->bounds));
    }
    return holds;
}

void QcStrongholdGenerator::planNear(QcBlockPos from, int reach) const
{
    const QList<std::shared_ptr<const QcStrongholdPlan>> plans = plansIn({from.x - reach, from.z - reach, from.x + reach, from.z + reach});
    Q_UNUSED(plans);
}

QcLocateResult QcStrongholdGenerator::nearest(QcBlockPos from, int reach) const
{
    if (!enabled()) {
        return {std::nullopt, reach};
    }
    // In an upgraded world the nearest may be one an older version left out; the next is asked.
    const std::optional<int> index = rings().nearest(from, [this](int candidate) { return m_terrain.buildsStructure(planOf(candidate)->bounds); });
    if (!index) {
        return {std::nullopt, reach};
    }
    return {QcLocatedPlace{planOf(*index)->middle(), std::nullopt}, reach};
}
