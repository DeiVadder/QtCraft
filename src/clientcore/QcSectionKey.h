#pragma once

#include "QcChunkPos.h"

// Packs a chunk section address into a single integer key for render-side maps.
constexpr quint64 qcSectionKey(QcChunkPos pos, int sectionIndex)
{
    return (quint64(quint32(pos.x) & 0xFFFFFFu) << 32) | (quint64(quint32(pos.z) & 0xFFFFFFu) << 8)
           | quint64(sectionIndex & 0xFF);
}
