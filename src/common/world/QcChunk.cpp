#include "common/world/QcChunk.h"

void QcChunk::setBlock(int x, int y, int z, QcBlockId block)
{
    Q_ASSERT(y >= 0 && y < QcWorld::worldHeight);
    m_sections[std::size_t(y / QcWorld::sectionHeight)].setBlock(x, y % QcWorld::sectionHeight, z, block);
}

int QcChunk::highestNonAir(int x, int z) const
{
    for (int section = QcWorld::sectionCount - 1; section >= 0; --section) {
        const QcChunkSection &data = m_sections[std::size_t(section)];
        if (data.isEmpty()) {
            continue;
        }
        for (int y = QcWorld::sectionHeight - 1; y >= 0; --y) {
            if (data.block(x, y, z) != qcAirBlockId) {
                return section * QcWorld::sectionHeight + y;
            }
        }
    }
    return -1;
}
