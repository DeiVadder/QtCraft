#pragma once

#include "common/core/QcBlockPos.h"
#include "common/world/QcChunkSection.h"

#include <array>

// Column of QcWorld::sectionCount sections at one chunk position.
class QcChunk
{
public:
    explicit QcChunk(QcChunkPos pos = {})
        : m_pos(pos)
    {
    }

    QcChunkPos pos() const { return m_pos; }

    // Local x/z in [0, chunkSize), y in [0, worldHeight). Outside the height range reads air.
    QcBlockId block(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
            return qcAirBlockId;
        }
        return m_sections[static_cast<std::size_t>(y / QcWorld::sectionHeight)].block(x, y % QcWorld::sectionHeight, z);
    }
    void setBlock(int x, int y, int z, QcBlockId block);

    const QcChunkSection &section(int index) const { return m_sections[static_cast<std::size_t>(index)]; }
    QcChunkSection &section(int index) { return m_sections[static_cast<std::size_t>(index)]; }

    // Highest y whose block is not air, or -1 for an empty column at (x, z).
    int highestNonAir(int x, int z) const;

private:
    QcChunkPos m_pos;
    std::array<QcChunkSection, QcWorld::sectionCount> m_sections;
};
