// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/fortress/QcFortressBlocks.h"
#include "worldgen/generators/QcChunkCanvas.h"

/*! \brief Writes the cells of a fortress that fall into one chunk (decision E-1070), with its
    worn bricks. */
class QcFortressCanvas : public QcChunkCanvas
{
public:
    /*! Writes into \a chunk with \a blocks; \a seed picks the worn bricks. */
    QcFortressCanvas(QcChunk &chunk, const QcFortressBlocks &blocks, quint64 seed);

    /*! Returns the blocks. */
    [[nodiscard]] const QcFortressBlocks &blocks() const { return m_blocks; }
    /*! Fills \a box with bricks, some of them worn. */
    void brick(const QcBlockBox &box);

private:
    const QcFortressBlocks &m_blocks;
};
