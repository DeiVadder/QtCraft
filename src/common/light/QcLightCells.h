// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcWorldConstants.h"

#include <array>

/*! \brief Coordinates of the cells of the light engine (QcLightEngine): the chunk and the place
    in it of a block coordinate, the index of a cell column in a chunk, the faces that lead to
    the neighbour columns. */
namespace QcLightCells {

/*! The four faces that lead to the neighbour columns. */
inline constexpr std::array horizontalFaces{QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

/*! Returns the chunk coordinate of the block coordinate \a block (rounding down for negative
    blocks). */
constexpr int floorChunk(int block)
{
    return block >= 0 ? block / QcWorld::chunkSize : (block + 1) / QcWorld::chunkSize - 1;
}

/*! Returns the place of the block coordinate \a block in its chunk, 0 to chunkSize - 1. */
constexpr int localOf(int block)
{
    return block - floorChunk(block) * QcWorld::chunkSize;
}

/*! Returns the index of the cell column (\a lx, \a lz) in its chunk: lz * chunkSize + lx. */
constexpr int columnIndex(int lx, int lz)
{
    return lz * QcWorld::chunkSize + lx;
}

} // namespace QcLightCells
