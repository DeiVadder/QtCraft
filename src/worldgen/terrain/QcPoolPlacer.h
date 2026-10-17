#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <QList>
#include <QSet>

#include <memory>
#include <optional>

class QcTerrainColumnGrid;
class QcTerrainGenerator;

// Liquid pools sunk into the ground: pools on cave floors, and pools with `surface` (lava lakes)
// in dry ground under open sky. Pools stay inside their chunk and never flow anywhere.
class QcPoolPlacer
{
public:
    using Pool = QcTerrainDefinitions::Pool;

    QcPoolPlacer(std::shared_ptr<const QcContentRegistry> content, const QcTerrainDefinitions &definitions);

    void placeInCaves(QcChunk &chunk) const;
    // Pools with `surface`, after the features so trees never depend on them.
    void placeOnSurface(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QcTerrainGenerator &terrain) const;

private:
    // The nearest cave floor at or below `start`: air standing on a block that holds liquid.
    [[nodiscard]] std::optional<int> caveFloor(const QcChunk &chunk, QcBlockPos start, int minY) const;
    // Sinks a pool below the air block `floor` (chunk-local); `attempt` varies its radius.
    void sink(QcChunk &chunk, const Pool &pool, QcBlockPos floor, int attempt) const;
    // One layer of a bowl: its centre (at the layer's height), index from the top and radius.
    struct BowlLayer
    {
        QcBlockPos centre;
        int layer = 0;
        double radius = 0.0;
    };

    // A bowl below the floor that narrows with depth and has a ragged rim, as cell keys.
    QSet<int> bowl(const QcChunk &chunk, const Pool &pool, QcBlockPos floor, int attempt) const;
    void addBowlLayer(const QcChunk &chunk, const Pool &pool, const BowlLayer &layer, QSet<int> &cells) const;
    // Liquid must not flow anywhere: drops cells until every remaining one is bounded below and on
    // its sides by other cells or by blocks that hold liquid.
    void dropUncontained(const QcChunk &chunk, QSet<int> &cells) const;
    // Only pools open to the cave (or sky) above are worth the liquid.
    bool opensUpwards(const QcChunk &chunk, const Pool &pool, const QSet<int> &cells) const;
    void fill(QcChunk &chunk, const Pool &pool, const QSet<int> &cells, int floorY) const;
    void hollowAbove(QcChunk &chunk, QcBlockPos cell, int floorY) const;
    // Whether a block holds a liquid in place (solid, neither liquid nor replaceable).
    bool holdsLiquid(QcBlockId block) const;
    bool isLiquid(QcBlockId block) const { return m_content->isValidBlock(block) && m_content->block(block).liquid; }

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Pool> m_pools;
    int m_seaLevel = 62;
    QcBlockId m_bedrock = qcAirBlockId;
};
