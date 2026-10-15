#pragma once

#include "QcChunkPos.h"

// Packs a chunk section address into a single integer key for render-side maps: 24 bits each for
// the chunk's x and z (two's complement), 8 bits for the section index.
constexpr quint64 qcSectionKey(QcChunkPos pos, int sectionIndex)
{
    return (quint64(quint32(pos.x) & 0xFFFFFFu) << 32) | (quint64(quint32(pos.z) & 0xFFFFFFu) << 8)
           | quint64(sectionIndex & 0xFF);
}

// The section address a key of qcSectionKey() stands for.
struct QcSectionAddress
{
    QcChunkPos chunk;
    int sectionIndex = 0;
};

constexpr QcSectionAddress qcSectionFromKey(quint64 key)
{
    // Sign-extends a 24-bit field.
    const auto field = [](quint64 bits) { return int(qint32(quint32(bits & 0xFFFFFFu) << 8) >> 8); };
    return {{field(key >> 32), field(key >> 8)}, int(key & 0xFFu)};
}

static_assert(qcSectionFromKey(qcSectionKey({-3, 5}, 4)).chunk == QcChunkPos{-3, 5});
static_assert(qcSectionFromKey(qcSectionKey({1'874'999, -1'875'000}, 15)).sectionIndex == 15);
