#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/mineshafts/QcMineshaftBuilder.h"
#include "worldgen/mineshafts/QcMineshaftPlanner.h"

#include <QHash>
#include <QMutex>

#include <memory>

// Builds the mine shafts of the terrain generator into its chunks (decision E-570): each chunk writes
// its own cells of the mine shaft of every region it overlaps (a mine shaft stays inside its region;
// QcMineshaftPlanner, plans cached by region, QcMineshaftBuilder), in any order with the same
// result. The spawners' kind, the chest minecarts with their loot and what rooms from structures
// bring along come with furnishingIn(). Thread-safe.
class QcMineshaftGenerator
{
public:
    QcMineshaftGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    const QcMineshaftPlanner &planner() const { return m_planner; }
    // The region's plan, planned once (null without a mine shaft).
    [[nodiscard]] std::shared_ptr<const QcMineshaftPlan> planOf(int regionX, int regionZ) const;
    // The mine shafts whose cells may lie in the chunk.
    QList<std::shared_ptr<const QcMineshaftPlan>> plansTouching(QcChunkPos pos) const;
    void build(QcChunk &chunk) const;
    // The surface entrances in the chunk, after the terrain's features and surface pools.
    void buildEntrances(QcChunk &chunk) const;
    // The chunk's spawners, carts and room contents.
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;

private:
    // The loot seed of a chest or cart at a cell.
    quint64 lootSeed(QcBlockPos at) const;
    // The chests, spawners and carts of a room from a structure in the chunk.
    [[nodiscard]] static QcChunkFurnishing roomFurnishing(QcChunkPos pos, const QcMineshaftPlan::Room &room, quint64 seed);

    QcMineshaftPlanner m_planner;
    QcMineshaftBuilder m_builder;
    quint64 m_seed = 0;
    mutable QMutex m_mutex;
    mutable QHash<qint64, std::shared_ptr<const QcMineshaftPlan>> m_plans;
};
