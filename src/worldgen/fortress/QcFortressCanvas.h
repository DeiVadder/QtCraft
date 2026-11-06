// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockBox.h"
#include "common/world/QcChunk.h"
#include "worldgen/fortress/QcFortressBlocks.h"

/*! \brief Writes the cells of a fortress that fall into one chunk (decision E-1070): everything
    takes world cells and leaves out what lies elsewhere, so the same calls in every chunk build
    the whole. Worn bricks follow from the cell alone. */
class QcFortressCanvas
{
public:
    /*! Writes into \a chunk with \a blocks; \a seed picks the worn bricks. */
    QcFortressCanvas(QcChunk &chunk, const QcFortressBlocks &blocks, quint64 seed);

    /*! Returns the blocks. */
    [[nodiscard]] const QcFortressBlocks &blocks() const { return m_blocks; }
    /*! Returns whether \a cell lies in the chunk. */
    [[nodiscard]] bool holds(QcBlockPos cell) const;
    /*! Returns whether \a box shares a cell with the chunk. */
    [[nodiscard]] bool reaches(const QcBlockBox &box) const;
    /*! Returns the block at \a cell, which lies in the chunk. */
    [[nodiscard]] QcBlockId at(QcBlockPos cell) const;
    /*! Places \a block at \a cell. */
    void set(QcBlockPos cell, QcBlockId block);
    /*! Fills \a box with \a block. */
    void fill(const QcBlockBox &box, QcBlockId block);
    /*! Fills \a box with bricks, some of them worn. */
    void brick(const QcBlockBox &box);
    /*! Returns the chunk. */
    [[nodiscard]] QcChunk &chunk() { return m_chunk; }

private:
    // The cells of `box` in the chunk; a box with low above high when there are none.
    [[nodiscard]] QcBlockBox clipped(const QcBlockBox &box) const;

    QcChunk &m_chunk;
    const QcFortressBlocks &m_blocks;
    quint64 m_seed = 0;
};
