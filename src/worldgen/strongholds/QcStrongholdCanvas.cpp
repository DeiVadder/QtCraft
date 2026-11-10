// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdCanvas.h"

QcStrongholdCanvas::QcStrongholdCanvas(QcChunk &chunk, const QcStrongholdBlocks &blocks, const QcStrongholdSettings &settings, quint64 seed)
    : QcChunkCanvas(chunk, seed)
    , m_blocks(blocks)
    , m_settings(settings)
{
}

QcBlockId QcStrongholdCanvas::brickAt(QcBlockPos cell) const
{
    // As in the original game: cracked, then mossy, then infested by share, the rest plain.
    const double draw = drawAt(cell);
    const double cracked = m_settings.crackedShare;
    const double mossy = cracked + m_settings.mossyShare;
    if (draw < cracked) {
        return blocks().cracked;
    }
    if (draw < mossy) {
        return blocks().mossy;
    }
    return draw < mossy + m_settings.infestedShare ? blocks().infested : blocks().bricks;
}

void QcStrongholdCanvas::wall(const QcBlockBox &box)
{
    const QcBlockBox cells = clipped(box);
    for (int y = cells.low.y; y <= cells.high.y; ++y) {
        for (int z = cells.low.z; z <= cells.high.z; ++z) {
            for (int x = cells.low.x; x <= cells.high.x; ++x) {
                set({x, y, z}, brickAt({x, y, z}));
            }
        }
    }
}

void QcStrongholdCanvas::put(const QcStrongholdPlan::Piece &piece, QcBlockPos local, QcBlockId block)
{
    set(piece.at(local.x, local.y, local.z), block);
}

void QcStrongholdCanvas::fill(const QcStrongholdPlan::Piece &piece, QcBlockPos low, QcBlockPos high, QcBlockId block)
{
    fill(piece.span(low, high), block);
}

void QcStrongholdCanvas::wall(const QcStrongholdPlan::Piece &piece, QcBlockPos low, QcBlockPos high)
{
    wall(piece.span(low, high));
}
