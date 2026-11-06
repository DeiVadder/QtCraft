// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/nether/QcNetherDefinitions.h"

class QcNetherTerrain;

/*! \brief What grows in the forests of the nether (decisions E-1045, E-1046): huge fungi, plants
    on the floors and columns of plants on floors and roofs.

    A huge fungus reaches into the chunks around it; each chunk asks the columns around it which
    fungi touch it, from the seed and the rock alone, and writes its own part. Thread-safe. */
class QcNetherGrowth
{
public:
    /*! Builds it from \a definitions and \a terrain, which outlive it. */
    QcNetherGrowth(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain);

    /*! Grows everything in \a chunk, whose ground is finished. */
    void grow(QcChunk &chunk) const;

private:
    // The fungus at the world column `x`, `z`, on every floor of the column it fits on.
    void growFungusColumn(QcChunk &chunk, const QcNetherDefinitions::Fungus &fungus, int x, int z) const;
    void growFungi(QcChunk &chunk) const;
    // The fungi of one kind in `biome` (its index) that reach into `chunk`.
    void growFungiOf(QcChunk &chunk, const QcNetherDefinitions::Fungus &fungus, qsizetype biome) const;
    // A floor at height `y` of the column: plants and rising strands.
    void growFloor(QcChunk &chunk, const QcNetherDefinitions::Biome &biome, QcBlockPos floor) const;
    // A roof at height `y` of the column: hanging strands.
    void growRoof(QcChunk &chunk, const QcNetherDefinitions::Biome &biome, QcBlockPos roof) const;
    void growColumn(QcChunk &chunk, int localX, int localZ) const;

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
};
