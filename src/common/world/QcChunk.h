// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/world/QcChunkSection.h"

#include <array>

/*! \brief Column of QcWorld::sectionCount sections at one chunk position. */
class QcChunk
{
public:
    explicit QcChunk(QcChunkPos pos = {})
        : m_pos(pos)
    {
    }

    /*! Returns the chunk's position. */
    QcChunkPos pos() const { return m_pos; }

    /*! Returns the block at local \a x and \a z in [0, chunkSize) and \a y in [0, worldHeight).
        Outside the height range reads air. */
    QcBlockId block(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
            return qcAirBlockId;
        }
        return m_sections[static_cast<std::size_t>(y / QcWorld::sectionHeight)].block(x, y % QcWorld::sectionHeight, z);
    }
    /*! Sets the block at local \a x, \a y and \a z to \a block. */
    void setBlock(int x, int y, int z, QcBlockId block);

    /*! Returns section \a index (0 at the bottom). */
    const QcChunkSection &section(int index) const { return m_sections[static_cast<std::size_t>(index)]; }
    /*! Returns section \a index (0 at the bottom) to change it. */
    QcChunkSection &section(int index) { return m_sections[static_cast<std::size_t>(index)]; }

    /*! Returns the highest y whose block is not air, or -1 for an empty column at (\a x, \a z). */
    int highestNonAir(int x, int z) const;

private:
    QcChunkPos m_pos;
    std::array<QcChunkSection, QcWorld::sectionCount> m_sections;
};
