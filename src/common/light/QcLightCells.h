// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcWorldConstants.h"

#include <array>

// Coordinates of the cells of the light engine (QcLightEngine): the chunk and the place in it of a
// block coordinate, the index of a cell column in a chunk, the faces that lead to the neighbour columns.
namespace QcLightCells {

inline constexpr std::array horizontalFaces{QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

// The chunk coordinate of a block coordinate (rounding down for negative blocks).
constexpr int floorChunk(int block)
{
    return block >= 0 ? block / QcWorld::chunkSize : (block + 1) / QcWorld::chunkSize - 1;
}

// The place of a block coordinate in its chunk, 0 to chunkSize - 1.
constexpr int localOf(int block)
{
    return block - floorChunk(block) * QcWorld::chunkSize;
}

constexpr int columnIndex(int lx, int lz)
{
    return lz * QcWorld::chunkSize + lx;
}

} // namespace QcLightCells
