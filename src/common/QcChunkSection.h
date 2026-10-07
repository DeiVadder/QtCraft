#pragma once

#include "QcBlockDefinition.h"
#include "QcWorldConstants.h"

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
    const QList<quint16> &indices() const { return m_indices; }
    // Replaces the content; returns false when the data is inconsistent (wrong size, index out of palette).
    bool assign(QList<QcBlockId> palette, QList<quint16> indices);
    // Drops unused palette entries and collapses to uniform storage when possible.
    void compact();

private:
    QList<QcBlockId> m_palette{qcAirBlockId};
    QList<quint16> m_indices;
};
