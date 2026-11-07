// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/blending/QcBlendField.h"

#include "common/core/QcBlockPos.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

// Beside a held rectangle the share reaches 1 after this many columns at most.
constexpr int widestFeather = 32;

// The squared distance between the nearest columns of `a` and `b`, 0 when they share one.
qint64 squaredDistance(QcXzRect a, QcXzRect b)
{
    const qint64 x = std::max({0, a.minX - b.maxX, b.minX - a.maxX});
    const qint64 z = std::max({0, a.minZ - b.maxZ, b.minZ - a.maxZ});
    return x * x + z * z;
}

// The share `distance` columns from the old land, `reach` columns being all the way.
float shareAt(double distance, int reach)
{
    const double t = std::clamp(distance / reach, 0.0, 1.0);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

// Whether a neighbour of `pos` is missing in `chunks`.
bool isOnTheEdge(const QcChunkSet &chunks, QcChunkPos pos)
{
    static constexpr std::array<QcChunkPos, 4> steps{QcChunkPos{1, 0}, QcChunkPos{-1, 0}, QcChunkPos{0, 1}, QcChunkPos{0, -1}};
    return std::ranges::any_of(steps, [&](QcChunkPos step) { return !chunks.contains({pos.x + step.x, pos.z + step.z}); });
}

} // namespace

QcBlendField::QcBlendField(const QcChunkSet &old, int widthChunks, QList<QcXzRect> held)
    : m_old(old)
    , m_widthChunks(widthChunks)
    , m_held(std::move(held))
{
    const QSet<QcChunkPos> chunks = old.chunks();
    for (const QcChunkPos chunk : chunks) {
        if (isOnTheEdge(old, chunk)) {
            m_edge.insert(chunk);
        }
    }
    for (const QcChunkPos chunk : std::as_const(m_edge)) {
        addChunksNear(QcXzRect::ofChunk(chunk), width());
    }
    for (const QcXzRect rect : std::as_const(m_held)) {
        addChunksNear(rect, feather());
    }
}

QcBlendField QcBlendField::heldWithinRing(QList<QcXzRect> held) const
{
    QcBlendField field = *this;
    field.m_held = std::move(held);
    field.m_cut = true;
    return field;
}

void QcBlendField::addChunksNear(QcXzRect edge, int reach)
{
    const qint64 limit = qint64{reach} * reach;
    const QList<QcChunkPos> near = edge.grown(reach).chunks();
    for (const QcChunkPos chunk : near) {
        if (chunk.isInsideWorld() && !m_old.contains(chunk) && squaredDistance(QcXzRect::ofChunk(chunk), edge) < limit) {
            m_ring.insert(chunk);
        }
    }
}

int QcBlendField::feather() const
{
    return std::min(width(), widestFeather);
}

QcBlendField::Shares QcBlendField::sharesIn(QcChunkPos pos) const
{
    const QcXzRect columns = QcXzRect::ofChunk(pos);
    QList<QcXzRect> edges;
    const QList<QcChunkPos> near = columns.grown(width()).chunks();
    for (const QcChunkPos chunk : near) {
        if (m_edge.contains(chunk)) {
            edges.append(QcXzRect::ofChunk(chunk));
        }
    }
    QList<QcXzRect> held;
    for (const QcXzRect rect : m_held) {
        if (rect.intersects(columns, feather())) {
            held.append(rect);
        }
    }

    const auto nearest = [](const QList<QcXzRect> &rects, QcXzRect column) {
        qint64 best = std::numeric_limits<qint64>::max();
        for (const QcXzRect rect : rects) {
            best = std::min(best, squaredDistance(rect, column));
        }
        return std::sqrt(static_cast<double>(best));
    };
    Shares shares{};
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            const QcXzRect column{columns.minX + x, columns.minZ + z, columns.minX + x, columns.minZ + z};
            const float byOld = shareAt(nearest(edges, column), width());
            const float byHeld = shareAt(nearest(held, column), feather());
            shares[static_cast<std::size_t>(columnIndex(x, z))] = std::min(byOld, byHeld);
        }
    }
    return shares;
}
