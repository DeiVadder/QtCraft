// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <QList>
#include <QSet>

#include <memory>
#include <optional>

/*! \brief Liquid pools sunk into cave floors.

    Pools stay inside their chunk and never flow anywhere; pools with \c {surface} are lakes
    (QcSurfaceLakes). */
class QcPoolPlacer
{
public:
    /*! A pool definition of the terrain. */
    using Pool = QcTerrainDefinitions::Pool;

    QcPoolPlacer(std::shared_ptr<const QcContentRegistry> content, const QcTerrainDefinitions &definitions);

    /*! Sinks the cave pools (those without \c {surface}) into the cave floors of \a chunk: per
        pool up to its attempts, each by its chance, at a random column off the chunk's edge and a
        start height between the pool's minY and maxY. */
    void placeInCaves(QcChunk &chunk) const;

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
    [[nodiscard]] QSet<int> bowl(const QcChunk &chunk, const Pool &pool, QcBlockPos floor, int attempt) const;
    // Whether the cell (x, z) of a layer of the bowl is dug out (chunk-local).
    [[nodiscard]] bool inBowl(const QcChunk &chunk, const Pool &pool, const BowlLayer &layer, int x, int z) const;
    // Liquid must not flow anywhere: the cells left after dropping cells until every remaining one is
    // bounded below and on its sides by other cells or by blocks that hold liquid.
    [[nodiscard]] QSet<int> contained(const QcChunk &chunk, QSet<int> cells) const;
    // Only pools open to the cave above are worth the liquid.
    bool opensUpwards(const QcChunk &chunk, const QSet<int> &cells) const;
    void fill(QcChunk &chunk, const Pool &pool, const QSet<int> &cells, int floorY) const;
    void hollowAbove(QcChunk &chunk, QcBlockPos cell, int floorY) const;
    // Whether a block holds a liquid in place (solid, neither liquid nor replaceable).
    bool holdsLiquid(QcBlockId block) const;
    bool isLiquid(QcBlockId block) const { return m_content->isValidBlock(block) && m_content->block(block).liquid; }

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Pool> m_pools;
    QcBlockId m_bedrock = qcAirBlockId;
};
