// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressCanvas.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>

namespace {

// The share of the bricks that are worn.
constexpr double wornShare = 0.07;

} // namespace

QcFortressCanvas::QcFortressCanvas(QcChunk &chunk, const QcFortressBlocks &blocks, quint64 seed)
    : m_chunk(chunk)
    , m_blocks(blocks)
    , m_seed(seed)
{
}

bool QcFortressCanvas::holds(QcBlockPos cell) const
{
    return cell.chunk() == m_chunk.pos() && cell.isInsideWorldHeight();
}

bool QcFortressCanvas::reaches(const QcBlockBox &box) const
{
    const int x = m_chunk.pos().x * QcWorld::chunkSize;
    const int z = m_chunk.pos().z * QcWorld::chunkSize;
    return box.low.x < x + QcWorld::chunkSize && box.high.x >= x && box.low.z < z + QcWorld::chunkSize && box.high.z >= z;
}

QcBlockId QcFortressCanvas::at(QcBlockPos cell) const
{
    return m_chunk.block(cell.x - m_chunk.pos().x * QcWorld::chunkSize, cell.y, cell.z - m_chunk.pos().z * QcWorld::chunkSize);
}

void QcFortressCanvas::set(QcBlockPos cell, QcBlockId block)
{
    if (holds(cell)) {
        m_chunk.setBlock(cell.x - m_chunk.pos().x * QcWorld::chunkSize, cell.y, cell.z - m_chunk.pos().z * QcWorld::chunkSize, block);
    }
}

QcBlockBox QcFortressCanvas::clipped(const QcBlockBox &box) const
{
    const int x = m_chunk.pos().x * QcWorld::chunkSize;
    const int z = m_chunk.pos().z * QcWorld::chunkSize;
    return {{std::max(box.low.x, x), std::max(box.low.y, 0), std::max(box.low.z, z)},
            {std::min(box.high.x, x + QcWorld::chunkSize - 1), std::min(box.high.y, QcWorld::worldHeight - 1), std::min(box.high.z, z + QcWorld::chunkSize - 1)}};
}

void QcFortressCanvas::fill(const QcBlockBox &box, QcBlockId block)
{
    const QcBlockBox cells = clipped(box);
    for (int y = cells.low.y; y <= cells.high.y; ++y) {
        for (int z = cells.low.z; z <= cells.high.z; ++z) {
            for (int x = cells.low.x; x <= cells.high.x; ++x) {
                set({x, y, z}, block);
            }
        }
    }
}

void QcFortressCanvas::brick(const QcBlockBox &box)
{
    const QcBlockBox cells = clipped(box);
    for (int y = cells.low.y; y <= cells.high.y; ++y) {
        for (int z = cells.low.z; z <= cells.high.z; ++z) {
            for (int x = cells.low.x; x <= cells.high.x; ++x) {
                set({x, y, z}, qcPositionRandom(m_seed, x, y, z) < wornShare ? m_blocks.cracked : m_blocks.brick);
            }
        }
    }
}
