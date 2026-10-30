// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcRiverField.h"
#include "worldgen/terrain/QcTerrainColumn.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

/*! \brief The ground blocks of a terrain column from its ground biome (decision E-858): the surface
    block (under water the bed with its patches, river banks, the land of deltas, beaches, snow
    above the snow line, rock on steep slopes) and the soil below it.

    Deterministic per seed and thread-safe. */
class QcTerrainSurface
{
public:
    /*! Reads the biomes from \a definitions and the patches of beds and banks from \a rivers;
        \a seed jitters the snow line. Both references must outlive the surface (the terrain
        generator owns all three). */
    QcTerrainSurface(const QcTerrainDefinitions &definitions, const QcRiverField &rivers, quint64 seed);

    /*! Returns the top block of \a column at \a x, \a z (its \c {slope} set where the biome has
        steep rock). */
    [[nodiscard]] QcBlockId surface(const QcTerrainColumn &column, int x, int z) const;
    /*! Returns the block of \a column below its \a surface block down to the subsurface depth:
        \a surface itself under water, on beaches, river banks and steep rock, else the biome's
        subsurface. */
    [[nodiscard]] QcBlockId subsurface(const QcTerrainColumn &column, QcBlockId surface) const;

private:
    using Biome = QcTerrainDefinitions::Biome;

    // A biome's own ground block `own` at a column, or one of its `patches` (block and share) there:
    // under water (decision E-840) and on the land of deltas (decision E-850).
    [[nodiscard]] QcBlockId patched(QcBlockId own, const QList<std::pair<QcBlockId, double>> &patches, int x, int z) const;

    const QcTerrainDefinitions &m_definitions;
    const QcRiverField &m_rivers;
    quint64 m_seed = 0;
};
