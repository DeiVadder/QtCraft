#pragma once

#include "common/core/QcWorldConstants.h"

#include <QTypeInfo>
#include <QtGlobal>

#include <compare>

// Horizontal position of a chunk column, in chunks.
struct QcChunkPos
{
    int x = 0;
    int z = 0;

    auto operator<=>(const QcChunkPos &) const = default;

    // Whether the chunk lies within the technical bound of the world (QcWorld::maxHorizontalChunk,
    // decision E-883); the server loads, generates and saves no chunk beyond it.
    constexpr bool isInsideWorld() const
    {
        return x >= -QcWorld::maxHorizontalChunk && x <= QcWorld::maxHorizontalChunk && z >= -QcWorld::maxHorizontalChunk
               && z <= QcWorld::maxHorizontalChunk;
    }
};

Q_DECLARE_TYPEINFO(QcChunkPos, Q_PRIMITIVE_TYPE);

size_t qHash(QcChunkPos pos, size_t seed = 0) noexcept;

// Chebyshev distance in chunks, used for view-distance rings.
int qcChunkDistance(QcChunkPos a, QcChunkPos b);
