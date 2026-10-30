// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <QList>

#include <functional>

class QcChunk;
class QcTerrainColumnGrid;
class QcTerrainGenerator;

/*! \brief Patches of one plant kind out of a biome's vegetation layer (decision E-814), as the
    reference's flower patches: each cell of QcVegetationPatches::patchCell blocks (aligned with the
    chunks) holds a patch per layer with the \c {chance} of the biome at a random centre in it; the
    patch tries \c {tries} columns around its centre, at most \c {radius} blocks away on each axis
    (more of them near the centre), and plants its one kind, chosen by weight, on every free column
    of that biome where the plant may stand.

    Deterministic per seed; thread-safe (only reads). */
class QcVegetationPatchPlacer
{
public:
    /*! \brief One patch: where it lies, its biome and layer, and the plant it plants. */
    struct Patch
    {
        /*! The world column of the patch's centre. \qcshared */
        int centreX = 0;
        int centreZ = 0;
        /*! The biome at the centre; the patch plants only on columns of this biome. */
        int biome = 0;
        /*! The vegetation layer the patch belongs to. */
        int layer = 0;
        /*! The biome's patch radius for this layer. */
        int radius = 0;
        /*! The plant of the patch, chosen by weight. */
        QcBlockId block = qcAirBlockId;
        /*! The columns it tries (x, z), centre first. */
        QList<std::pair<int, int>> tries;
    };
    /*! Whether a column may get plants at all (not in a village or under a structure). */
    using ColumnFilter = std::function<bool(int x, int z)>;

    QcVegetationPatchPlacer(const QcTerrainGenerator &generator, const QcTerrainDefinitions &definitions, quint64 seed);

    /*! Returns the patches whose columns may reach into the chunk at \a pos. */
    [[nodiscard]] QList<Patch> patchesNear(QcChunkPos pos) const;
    /*! Plants the patches into \a chunk (after the features: only into air), on the terrain
        \a columns of dry land of each patch's biome. \a carved: per column of the chunk
        (z * chunkSize + x), whether caves removed its top block; \a allowed filters the
        columns. */
    void place(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved, const ColumnFilter &allowed) const;

private:
    // The patch of `layer` in the cell (cellX, cellZ), if it has one.
    [[nodiscard]] std::optional<Patch> patchIn(int cellX, int cellZ, int layer) const;

    const QcTerrainGenerator &m_generator;
    const QcTerrainDefinitions &m_definitions;
    std::array<quint64, QcTerrainDefinitions::VegetationLayers> m_seeds{};
};
