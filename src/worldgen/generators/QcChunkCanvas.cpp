// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcChunkCanvas.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>

QcChunkCanvas::QcChunkCanvas(QcChunk &chunk, quint64 seed)
    : m_chunk(chunk)
    , m_seed(seed)
{
}

bool QcChunkCanvas::holds(QcBlockPos cell) const
{
    return cell.chunk() == m_chunk.pos() && cell.isInsideWorldHeight();
}

bool QcChunkCanvas::reaches(const QcBlockBox &box) const
{
    const int x = m_chunk.pos().x * QcWorld::chunkSize;
    const int z = m_chunk.pos().z * QcWorld::chunkSize;
    return box.low.x < x + QcWorld::chunkSize && box.high.x >= x && box.low.z < z + QcWorld::chunkSize && box.high.z >= z;
}

QcBlockId QcChunkCanvas::at(QcBlockPos cell) const
{
    return m_chunk.block(cell.x - m_chunk.pos().x * QcWorld::chunkSize, cell.y, cell.z - m_chunk.pos().z * QcWorld::chunkSize);
}

void QcChunkCanvas::set(QcBlockPos cell, QcBlockId block)
{
    if (holds(cell)) {
        m_chunk.setBlock(cell.x - m_chunk.pos().x * QcWorld::chunkSize, cell.y, cell.z - m_chunk.pos().z * QcWorld::chunkSize, block);
    }
}

QcBlockBox QcChunkCanvas::clipped(const QcBlockBox &box) const
{
    const int x = m_chunk.pos().x * QcWorld::chunkSize;
    const int z = m_chunk.pos().z * QcWorld::chunkSize;
    return {{std::max(box.low.x, x), std::max(box.low.y, 0), std::max(box.low.z, z)},
            {std::min(box.high.x, x + QcWorld::chunkSize - 1), std::min(box.high.y, QcWorld::worldHeight - 1), std::min(box.high.z, z + QcWorld::chunkSize - 1)}};
}

void QcChunkCanvas::fill(const QcBlockBox &box, QcBlockId block)
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

double QcChunkCanvas::drawAt(QcBlockPos cell) const
{
    return qcPositionRandom(m_seed, cell.x, cell.y, cell.z);
}
