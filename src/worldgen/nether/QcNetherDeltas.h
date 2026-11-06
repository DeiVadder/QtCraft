// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/nether/QcNetherDefinitions.h"

class QcNetherTerrain;

/*! \brief What makes the basalt deltas (decision E-1047): the rock of their columns in bands,
    still pools of lava with rims of magma, and stone posts on floors and under roofs.

    A pool's lava only lies in cells with rock on all four sides, here or in the chunks around, so
    it stays where it is without a block update. Every column depends on nothing but the seed and
    the rock. Thread-safe. */
class QcNetherDeltas
{
public:
    /*! Builds it from \a definitions and \a terrain, which outlive it. */
    QcNetherDeltas(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain);

    /*! Works \a chunk, whose ground, ores and clusters are finished. */
    void shape(QcChunk &chunk) const;

private:
    // Whether the cell `local` keeps a liquid in: inside the chunk anything but air, outside it the
    // rock of the terrain.
    [[nodiscard]] bool holdsLiquid(const QcChunk &chunk, QcBlockPos local) const;
    void rockColumn(QcChunk &chunk, const QcNetherDefinitions::Biome &biome, int localX, int localZ) const;
    void poolColumn(QcChunk &chunk, const QcNetherDefinitions::Pool &pool, int localX, int localZ) const;
    void pillarColumn(QcChunk &chunk, const QcNetherDefinitions::Pillar &pillar, int localX, int localZ) const;
    void shapeColumn(QcChunk &chunk, int localX, int localZ) const;

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
};
