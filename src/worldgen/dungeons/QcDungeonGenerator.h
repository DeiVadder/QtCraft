#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/dungeons/QcDungeonPlanner.h"
#include "worldgen/generators/QcChunkFurnishing.h"

#include <QHash>
#include <QMutex>

#include <memory>
#include <vector>

// Builds the dungeons of the terrain generator into its chunks (decision E-522): each chunk writes
// its own cells of every dungeon whose plan touches it (QcDungeonPlanner, plans cached by region),
// in any order with the same result. The room is a shell of wall and moss blocks around air; the
// corridors' lining replaces only what is not rock (cave air, liquids, earth), then the corridors are
// carved (liquid beside them sealed with wall) and the spawner, chests and lights placed. The chests' loot and the spawner's kind come
// with furnishingIn(). Thread-safe.
class QcDungeonGenerator
{
public:
    QcDungeonGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    const QcDungeonPlanner &planner() const { return m_planner; }
    // The region's plan, planned once (null without a dungeon).
    [[nodiscard]] std::shared_ptr<const QcDungeonPlan> planOf(int regionX, int regionZ) const;
    // The dungeons whose cells may lie in the chunk.
    QList<std::shared_ptr<const QcDungeonPlan>> plansTouching(QcChunkPos pos) const;
    void build(QcChunk &chunk) const;
    QcChunkFurnishing furnishingIn(QcChunkPos pos) const;

private:
    void build(QcChunk &chunk, const QcDungeonPlan &plan) const;
    // Turns liquid of the chunk next to the corridors into wall.
    void sealLiquids(QcChunk &chunk, const QcDungeonPlan &plan) const;
    // A wall block at `pos`: moss with the settings' share (twice that on the floor).
    QcBlockId wallAt(QcBlockPos pos, bool floor) const;

    QcDungeonPlanner m_planner;
    quint64 m_seed = 0;
    QString m_lootTable;
    // Per block state: rock that corridors keep as their wall (solid, opaque, mined with a pickaxe).
    std::vector<bool> m_rock;
    std::vector<bool> m_liquid;
    mutable QMutex m_mutex;
    mutable QHash<qint64, std::shared_ptr<const QcDungeonPlan>> m_plans;
};
