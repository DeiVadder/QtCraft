// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcWorldConstants.h"
#include "worldgen/terrain/QcTerrainColumn.h"

#include <QList>

class QcTerrainGenerator;

/*! \brief Columns of a chunk and a margin around it, computed once per chunk, with their slopes. */
class QcTerrainColumnGrid
{
public:
    /*! Trees reach this far from their trunk, so neighbouring columns are scanned for features. */
    static constexpr int featureMargin = 3;
    /*! Columns computed around a chunk: features plus one more for their water and slope checks. */
    static constexpr int margin = featureMargin + 1;
    /*! Columns along each side of the grid: the chunk plus \c {margin} on both sides. */
    static constexpr int size = QcWorld::chunkSize + 2 * margin;

    QcTerrainColumnGrid(const QcTerrainGenerator &generator, QcChunkPos pos);

    /*! Returns the column at the world coordinates \a x, \a z, which lie within the chunk plus
        \c {margin}. */
    const QcTerrainColumn &at(int x, int z) const { return m_columns[(z - m_baseZ) * size + (x - m_baseX)]; }
    /*! Sets the height of the column at \a x, \a z to \a height: moves a column's ground (the
        blend around a surface structure, decision E-889), so features stand on the ground as
        built. */
    void setHeight(int x, int z, int height) { m_columns[(z - m_baseZ) * size + (x - m_baseX)].height = height; }
    /*! Returns whether the world column \a x, \a z lies in the grid. */
    bool contains(int x, int z) const { return x >= m_baseX && x < m_baseX + size && z >= m_baseZ && z < m_baseZ + size; }

private:
    // Looks up the ground biome (decision E-858) of every column near a border whose ground source
    // lies in the grid, and of every column of the chunk.
    void lookUpGround(const QcTerrainGenerator &generator);

    int m_baseX = 0;
    int m_baseZ = 0;
    QList<QcTerrainColumn> m_columns;
};
