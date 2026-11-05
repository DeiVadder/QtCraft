// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/pathing/QcPathTerrain.h"

#include <vector>

/*! \brief The cells around the start of one search (decision E-400): a box of 64 x 32 x 64 cells,
    each read from the world at most once per search, and the A* bookkeeping of its nodes.

    Arrays are allocated with the first search and reused; a generation number marks what belongs
    to the current search, so starting a search clears nothing. Cells outside the box count as
    unknown: a search never leaves it. */
class QcPathWindow
{
public:
    /*! Cells of the box on each side of the centre along x and z. */
    static constexpr int halfWidth = 32;
    /*! Cells of the box above and below the centre. */
    static constexpr int halfHeight = 16;
    /*! The width of the box along x and z. */
    static constexpr int sizeX = 2 * halfWidth;
    /*! The height of the box. */
    static constexpr int sizeY = 2 * halfHeight;
    /*! The number of cells of the box. */
    static constexpr int cellCount = sizeX * sizeX * sizeY;

    /*! Starts a search around \a centre in \a world, path cells from \a table. */
    void begin(const QcBlockAccess &world, const QcPathCellTable &table, QcBlockPos centre);

    /*! Returns the index of the cell at \a pos inside the box; -1 outside. */
    int indexOf(QcBlockPos pos) const;
    /*! Returns the position of the cell with the index \a index (indexOf()). */
    QcBlockPos positionOf(int index) const;
    /*! Returns the cell at \a pos (QcPathCell), read from the world on first use. */
    quint8 cell(QcBlockPos pos);
    /*! Returns the gap of an open door or trapdoor in the cell at \a pos, straight from the
        world (only cells with a panel, QcPathCell::hasPanel, ask for it; few enough not to
        cache). */
    QcPathGap gap(QcBlockPos pos) const { return direct().gap(pos); }

    /*! Returns whether this search visited the node \a index (A* state). */
    bool visited(int index) const { return m_nodeStamp[static_cast<std::size_t>(index)] == m_generation; }
    /*! Returns whether this search closed the node \a index. */
    bool closed(int index) const { return visited(index) && m_closed[static_cast<std::size_t>(index)] != 0; }
    /*! Returns the cost of the way to the visited node \a index. */
    float cost(int index) const { return m_cost[static_cast<std::size_t>(index)]; }
    /*! Returns the node the way to the visited node \a index comes from. */
    int parent(int index) const { return m_parent[static_cast<std::size_t>(index)]; }
    /*! Visits the node \a index with the way's \a cost from \a parent. */
    void open(int index, float cost, int parent);
    /*! Closes the visited node \a index. */
    void close(int index) { m_closed[static_cast<std::size_t>(index)] = 1; }

private:
    // The world of this search, read without the cache.
    QcPathDirectCells direct() const { return {*m_world, *m_table}; }

    const QcBlockAccess *m_world = nullptr;
    const QcPathCellTable *m_table = nullptr;
    QcBlockPos m_origin;
    quint32 m_generation = 0;
    std::vector<quint32> m_cellStamp;
    std::vector<quint8> m_cells;
    std::vector<quint32> m_nodeStamp;
    std::vector<float> m_cost;
    std::vector<int> m_parent;
    std::vector<quint8> m_closed;
};
