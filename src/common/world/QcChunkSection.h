#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcWorldConstants.h"

#include <QList>

// 16x16x16 blocks stored as a palette plus per-block palette indices. A section with a single
// block type stores no index array, so empty (all-air) sections cost almost nothing.
class QcChunkSection
{
public:
    static constexpr int index(int x, int y, int z) { return (y * QcWorld::chunkSize + z) * QcWorld::chunkSize + x; }

    QcBlockId block(int x, int y, int z) const;
    void setBlock(int x, int y, int z, QcBlockId block);
    void fill(QcBlockId block);

    bool isUniform() const { return m_indices.isEmpty(); }
    bool isEmpty() const { return isUniform() && m_palette.constFirst() == qcAirBlockId; }

    const QList<QcBlockId> &palette() const { return m_palette; }
    // Bytes the palette and the indices hold on the heap (their capacity; shared copies count in
    // full for each holder), for memory statistics.
    qsizetype heapBytes() const
    {
        return m_palette.capacity() * qsizetype(sizeof(QcBlockId)) + m_indices.capacity() * qsizetype(sizeof(quint16));
    }

    // Calls `visit(pos)` for every cell whose block is `wanted`, with the section's lowest corner at
    // `corner` in the world.
    template<typename Wanted, typename Visit>
    void forEachCell(QcBlockPos corner, Wanted &&wanted, Visit &&visit) const
    {
        constexpr int size = QcWorld::chunkSize;
        for (int cell = 0; cell < size * size * QcWorld::sectionHeight; ++cell) {
            const int x = cell % size;
            const int z = cell / size % size;
            const int y = cell / (size * size);
            if (wanted(block(x, y, z))) {
                visit(corner.offset(x, y, z));
            }
        }
    }
    const QList<quint16> &indices() const { return m_indices; }
    // Replaces the content; returns false when the data is inconsistent (wrong size, index out of palette).
    bool assign(QList<QcBlockId> palette, QList<quint16> indices);
    // Drops unused palette entries and collapses to uniform storage when possible.
    void compact();

private:
    QList<QcBlockId> m_palette{qcAirBlockId};
    QList<quint16> m_indices;
};
