// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/blending/QcBlendGrid.h"

#include <memory>
#include <optional>

class QcWorldGenerator;

/*! \brief Builds one chunk of a ring from both versions, column by column (decision E-1041).

    Below the top few blocks of the blended ground lies the underground of the nearer version with
    its caves and ores; a version's ground that ends lower is filled up with its deep block. The
    top blocks come from the version a patch chooses by the share, moved to the blended height;
    above them comes the water of the grid.

    What stands on the ground stays whole: a tree (QcChunkShape::Root) goes with the choice at its
    trunk and moves by the height its trunk moved, its crown with it, column by column, so it
    needs no block of another column. Small plants go with their own column, ice and water plants
    with the water. */
class QcBlendedChunk
{
public:
    /*! \brief A chunk of the ring in both versions. \qcfields */
    struct Versions
    {
        const QcChunk &older;
        const QcChunk &newer;
    };

    /*! Prepares the chunk of \a versions by \a grid, with the shapes of the ring in \a sources,
        judging blocks by \a generator. */
    QcBlendedChunk(Versions versions, const QcBlendSources &sources, const QcBlendGrid &grid, const QcWorldGenerator &generator);

    /*! Returns the blended chunk. */
    [[nodiscard]] std::shared_ptr<QcChunk> build() const;

private:
    // One of the two versions of the chunk.
    struct Side
    {
        const QcChunk *chunk = nullptr;
        const QcChunkShape *shape = nullptr;
        bool newer = false;
    };
    // How a block standing on the ground moves.
    struct Move
    {
        bool kept = false;
        int shift = 0;
    };

    [[nodiscard]] Side side(bool newer) const;
    [[nodiscard]] int worldX(int localX) const { return m_pos.x * QcWorld::chunkSize + localX; }
    [[nodiscard]] int worldZ(int localZ) const { return m_pos.z * QcWorld::chunkSize + localZ; }
    // The block filling a column of `side` below its surface layers.
    [[nodiscard]] QcBlockId fillerOf(const Side &side, int x, int z) const;
    // Writes ground and water of the column `x`, `z` into `out`.
    void writeGround(QcChunk &out, int x, int z, const QcBlendGrid::Cell &cell) const;
    // Copies the column `x`, `z` whole from its version, but for the caves beside the water.
    void writeWhole(QcChunk &out, int x, int z, const QcBlendGrid::Cell &cell) const;
    // Writes what stands in the column `x`, `z` of `side` into `out`.
    void writeStanding(QcChunk &out, const Side &side, int x, int z) const;
    // The move of the plant in the column itself.
    [[nodiscard]] Move ownMove(const Side &side, QcColumnShape shape, const QcBlendGrid::Cell &cell) const;
    // The move of the crown over the column `x`, `z`: its nearest trunk's.
    [[nodiscard]] Move crownMove(const Side &side, int x, int z) const;
    // The nearest tree of `side` to the column `x`, `z`, in this chunk and around it.
    [[nodiscard]] std::optional<QcChunkShape::Root> nearestRoot(const Side &side, int x, int z) const;

    Versions m_versions;
    QcChunkPos m_pos;
    const QcBlendSources &m_sources;
    const QcBlendGrid &m_grid;
    const QcWorldGenerator &m_generator;
    const QcBlendSource &m_source;
};
