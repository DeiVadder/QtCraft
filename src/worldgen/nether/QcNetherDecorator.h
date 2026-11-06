// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/nether/QcNetherDefinitions.h"

#include <QList>

#include <optional>

class QcNetherTerrain;

/*! \brief What the nether generator lays over the bare rock of a chunk (decision E-1030): the
    ground of its biomes, the banks along the lava, ore veins, glowstone on the roofs, springs and
    natural fires.

    Everything stays inside the chunk and depends on nothing but the chunk and the seed.
    Thread-safe. */
class QcNetherDecorator
{
public:
    /*! Builds the decorator from \a definitions and \a terrain, which outlive it. */
    QcNetherDecorator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain);

    /*! Decorates \a chunk, which holds the bare terrain. */
    void decorate(QcChunk &chunk) const;
    /*! Opens the springs of \a chunk; after the structures, so that no fall ends on one. */
    void placeSprings(QcChunk &chunk) const;
    /*! Returns the springs of the generated \a chunk: the cells the server gives a block update,
        so their liquid starts to flow when the chunk arrives (world coordinates). */
    [[nodiscard]] QList<QcBlockPos> springsIn(const QcChunk &chunk) const;
    /*! Returns the roll in 0 to 1 that picks the patch of the ground of \a biome at the column
        \a x, \a z: by hash, or by noise for a biome with \c {patchScale}. */
    [[nodiscard]] double patchRoll(const QcNetherDefinitions::Biome &biome, int x, int z) const;

private:
    // The ground of the biomes and the banks of the column `localX`, `localZ`.
    void groundColumn(QcChunk &chunk, int localX, int localZ) const;
    void shoreColumn(QcChunk &chunk, int localX, int localZ) const;
    void placeOres(QcChunk &chunk) const;
    void placeCluster(QcChunk &chunk, const QcNetherDefinitions::Cluster &cluster, int attempt) const;
    // Whether what runs out at `cell` falls straight into the sea: a thin fall, where a ledge on
    // the way would spread it into a curtain.
    [[nodiscard]] bool fallsIntoTheSea(const QcChunk &chunk, QcBlockPos cell) const;
    void placeSpring(QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const;
    void placeFire(QcChunk &chunk, const QcNetherDefinitions::Fire &fire, int attempt) const;
    // The cell a try of `spring` looks at first and walks from along x.
    [[nodiscard]] QcBlockPos springStart(const QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const;
    // The side of the cave a try of `spring` looks for, as a step.
    [[nodiscard]] QcBlockPos springSide(const QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const;
    // The spring of that try in the finished `chunk`, in world coordinates, if it opened.
    [[nodiscard]] std::optional<QcBlockPos> springOf(const QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const;

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
};
