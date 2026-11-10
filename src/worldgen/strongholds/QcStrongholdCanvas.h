// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStrongholdDefinition.h"
#include "worldgen/generators/QcChunkCanvas.h"
#include "worldgen/strongholds/QcStrongholdBlocks.h"

/*! \brief Writes the cells of a stronghold that fall into one chunk (decision E-1171): cells in
    the frame of a piece, and walls of bricks of which some are cracked, mossy or infested. */
class QcStrongholdCanvas : public QcChunkCanvas
{
public:
    /*! Writes into \a chunk with \a blocks and the shares of worn bricks of \a settings, both of
        which outlive it; \a seed picks the worn bricks. */
    QcStrongholdCanvas(QcChunk &chunk, const QcStrongholdBlocks &blocks, const QcStrongholdSettings &settings, quint64 seed);

    /*! Returns the blocks. */
    [[nodiscard]] const QcStrongholdBlocks &blocks() const { return m_blocks; }
    /*! Returns the brick of a wall at \a cell: plain, cracked, mossy or infested. */
    [[nodiscard]] QcBlockId brickAt(QcBlockPos cell) const;
    /*! Fills \a box with the bricks of a wall. */
    void wall(const QcBlockBox &box);
    /*! Places \a block at the cell \a local of the frame of \a piece. */
    void put(const QcStrongholdPlan::Piece &piece, QcBlockPos local, QcBlockId block);
    /*! Fills the cells from \a low to \a high of the frame of \a piece with \a block. */
    void fill(const QcStrongholdPlan::Piece &piece, QcBlockPos low, QcBlockPos high, QcBlockId block);
    /*! Fills the cells from \a low to \a high of the frame of \a piece with the bricks of a
        wall. */
    void wall(const QcStrongholdPlan::Piece &piece, QcBlockPos low, QcBlockPos high);

    using QcChunkCanvas::fill;

private:
    const QcStrongholdBlocks &m_blocks;
    const QcStrongholdSettings &m_settings;
};
