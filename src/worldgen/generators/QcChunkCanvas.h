// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockBox.h"
#include "common/world/QcChunk.h"

/*! \brief Writes the cells of a planned structure that fall into one chunk (decision E-1070):
    everything takes world cells and leaves out what lies elsewhere, so the same calls in every
    chunk build the whole. A mottled wall follows from the cell alone. */
class QcChunkCanvas
{
public:
    /*! Writes into \a chunk; \a seed picks the cells of a mottled wall. */
    QcChunkCanvas(QcChunk &chunk, quint64 seed);

    /*! Returns whether \a cell lies in the chunk. */
    [[nodiscard]] bool holds(QcBlockPos cell) const;
    /*! Returns whether \a box shares a column with the chunk. */
    [[nodiscard]] bool reaches(const QcBlockBox &box) const;
    /*! Returns the block at \a cell, which lies in the chunk. */
    [[nodiscard]] QcBlockId at(QcBlockPos cell) const;
    /*! Places \a block at \a cell. */
    void set(QcBlockPos cell, QcBlockId block);
    /*! Fills \a box with \a block. */
    void fill(const QcBlockBox &box, QcBlockId block);
    /*! Returns the cells of \a box in the chunk; a box with low above high when there are none. */
    [[nodiscard]] QcBlockBox clipped(const QcBlockBox &box) const;
    /*! Returns a draw from 0 to 1 that follows from \a cell and the seed alone. */
    [[nodiscard]] double drawAt(QcBlockPos cell) const;
    /*! Returns the chunk. */
    [[nodiscard]] QcChunk &chunk() { return m_chunk; }

private:
    QcChunk &m_chunk;
    quint64 m_seed = 0;
};
