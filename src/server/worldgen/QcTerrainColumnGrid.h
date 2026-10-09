#pragma once

#include "QcChunkPos.h"
#include "QcTerrainColumn.h"
#include "QcWorldConstants.h"

#include <QList>

class QcTerrainGenerator;

// Columns of a chunk and a margin around it, computed once per chunk, with their slopes.
class QcTerrainColumnGrid
{
public:
    // Trees reach this far from their trunk, so neighbouring columns are scanned for features.
    static constexpr int featureMargin = 3;
    // Columns computed around a chunk: features plus one more for their water and slope checks.
    static constexpr int margin = featureMargin + 1;
    static constexpr int size = QcWorld::chunkSize + 2 * margin;

    QcTerrainColumnGrid(const QcTerrainGenerator &generator, QcChunkPos pos);

    // World coordinates within the chunk plus `margin`.
    const QcTerrainColumn &at(int x, int z) const { return m_columns[(z - m_baseZ) * size + (x - m_baseX)]; }
    bool contains(int x, int z) const { return x >= m_baseX && x < m_baseX + size && z >= m_baseZ && z < m_baseZ + size; }

private:
    int m_baseX = 0;
    int m_baseZ = 0;
    QList<QcTerrainColumn> m_columns;
};
