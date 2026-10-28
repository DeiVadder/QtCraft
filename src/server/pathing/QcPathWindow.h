#pragma once

#include "server/pathing/QcPathTerrain.h"

#include <vector>

// The cells around the start of one search (decision E-400): a box of 64 x 32 x 64 cells, each read
// from the world at most once per search, and the A* bookkeeping of its nodes. Arrays are allocated
// with the first search and reused; a generation number marks what belongs to the current search,
// so starting a search clears nothing. Cells outside the box count as unknown: a search never
// leaves it.
class QcPathWindow
{
public:
    static constexpr int halfWidth = 32;
    static constexpr int halfHeight = 16;
    static constexpr int sizeX = 2 * halfWidth;
    static constexpr int sizeY = 2 * halfHeight;
    static constexpr int cellCount = sizeX * sizeX * sizeY;

    // Starts a search around `centre`.
    void begin(const QcBlockAccess &world, const QcPathCellTable &table, QcBlockPos centre);

    // Index of a cell inside the box; -1 outside.
    int indexOf(QcBlockPos pos) const;
    QcBlockPos positionOf(int index) const;
    // The cell (QcPathCell), read from the world on first use.
    quint8 cell(QcBlockPos pos);
    // The gap of an open door or trapdoor in the cell, straight from the world (only cells with a
    // panel, QcPathCell::hasPanel, ask for it; few enough not to cache).
    QcPathGap gap(QcBlockPos pos) const { return direct().gap(pos); }

    // A* state of a node of this search.
    bool visited(int index) const { return m_nodeStamp[static_cast<std::size_t>(index)] == m_generation; }
    bool closed(int index) const { return visited(index) && m_closed[static_cast<std::size_t>(index)] != 0; }
    float cost(int index) const { return m_cost[static_cast<std::size_t>(index)]; }
    int parent(int index) const { return m_parent[static_cast<std::size_t>(index)]; }
    void open(int index, float cost, int parent);
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
