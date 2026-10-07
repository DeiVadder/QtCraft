#pragma once

#include <QtGlobal>

#include <compare>

// Horizontal position of a chunk column, in chunks.
struct QcChunkPos
{
    int x = 0;
    int z = 0;

    auto operator<=>(const QcChunkPos &) const = default;
};

size_t qHash(QcChunkPos pos, size_t seed = 0) noexcept;

// Chebyshev distance in chunks, used for view-distance rings.
int qcChunkDistance(QcChunkPos a, QcChunkPos b);
