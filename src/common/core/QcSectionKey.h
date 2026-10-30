// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

/*! Returns the key of section \a sectionIndex of the chunk at \a pos: packs the address into a
    single integer for render-side maps, 28 bits each for the chunk's x and z (two's complement;
    chunks reach ±QcWorld::maxHorizontalChunk, which needs 27, decision E-883), 8 bits for the
    section index. */
constexpr quint64 qcSectionKey(QcChunkPos pos, int sectionIndex)
{
    return (static_cast<quint64>(static_cast<quint32>(pos.x) & 0xFFFFFFFu) << 36) | (static_cast<quint64>(static_cast<quint32>(pos.z) & 0xFFFFFFFu) << 8)
           | static_cast<quint64>(sectionIndex & 0xFF);
}

/*! \brief The section address a key of qcSectionKey() stands for. */
struct QcSectionAddress
{
    /*! The chunk. */
    QcChunkPos chunk;
    /*! The section's index in the chunk. */
    int sectionIndex = 0;
};

/*! Returns the section address \a key stands for (the inverse of qcSectionKey()). */
constexpr QcSectionAddress qcSectionFromKey(quint64 key)
{
    // Sign-extends a 28-bit field.
    const auto field = [](quint64 bits) { return static_cast<int>(static_cast<qint32>(static_cast<quint32>(bits & 0xFFFFFFFu) << 4) >> 4); };
    return {{field(key >> 36), field(key >> 8)}, static_cast<int>(key & 0xFFu)};
}

static_assert(qcSectionFromKey(qcSectionKey({-3, 5}, 4)).chunk == QcChunkPos{-3, 5});
static_assert(qcSectionFromKey(qcSectionKey({1'874'999, -1'875'000}, 15)).sectionIndex == 15);
static_assert(qcSectionFromKey(qcSectionKey({QcWorld::maxHorizontalChunk, -QcWorld::maxHorizontalChunk}, 15)).chunk
              == QcChunkPos{QcWorld::maxHorizontalChunk, -QcWorld::maxHorizontalChunk});
