// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressCanvas.h"

namespace {

// The share of the bricks that are worn.
constexpr double wornShare = 0.07;

} // namespace

QcFortressCanvas::QcFortressCanvas(QcChunk &chunk, const QcFortressBlocks &blocks, quint64 seed)
    : QcChunkCanvas(chunk, seed)
    , m_blocks(blocks)
{
}

void QcFortressCanvas::brick(const QcBlockBox &box)
{
    const QcBlockBox cells = clipped(box);
    for (int y = cells.low.y; y <= cells.high.y; ++y) {
        for (int z = cells.low.z; z <= cells.high.z; ++z) {
            for (int x = cells.low.x; x <= cells.high.x; ++x) {
                set({x, y, z}, drawAt({x, y, z}) < wornShare ? blocks().cracked : blocks().brick);
            }
        }
    }
}
