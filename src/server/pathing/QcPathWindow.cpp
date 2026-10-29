// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/pathing/QcPathWindow.h"

#include <algorithm>

void QcPathWindow::begin(const QcBlockAccess &world, const QcPathCellTable &table, QcBlockPos centre)
{
    // The arrays (about 2 MB) come with the first search: a server without creatures does without.
    if (m_cells.empty()) {
        m_cellStamp.assign(cellCount, 0);
        m_cells.assign(cellCount, 0);
        m_nodeStamp.assign(cellCount, 0);
        m_cost.assign(cellCount, 0.0f);
        m_parent.assign(cellCount, -1);
        m_closed.assign(cellCount, 0);
    }

    m_world = &world;
    m_table = &table;
    m_origin = centre.offset(-halfWidth, -halfHeight, -halfWidth);

    // After four billion searches the stamps start over from a cleared state.
    ++m_generation;
    if (m_generation == 0) {
        std::ranges::fill(m_cellStamp, 0u);
        std::ranges::fill(m_nodeStamp, 0u);
        m_generation = 1;
    }
}

int QcPathWindow::indexOf(QcBlockPos pos) const
{
    const int x = pos.x - m_origin.x;
    const int y = pos.y - m_origin.y;
    const int z = pos.z - m_origin.z;
    if (x < 0 || x >= sizeX || z < 0 || z >= sizeX || y < 0 || y >= sizeY) {
        return -1;
    }
    return (y * sizeX + z) * sizeX + x;
}

QcBlockPos QcPathWindow::positionOf(int index) const
{
    const int x = index % sizeX;
    const int z = (index / sizeX) % sizeX;
    const int y = index / (sizeX * sizeX);
    return m_origin.offset(x, y, z);
}

quint8 QcPathWindow::cell(QcBlockPos pos)
{
    const int index = indexOf(pos);
    if (index < 0) {
        return QcPathCell::make(QcPathCell::Kind::Unknown);
    }

    const std::size_t at = static_cast<std::size_t>(index);
    if (m_cellStamp[at] != m_generation) {
        m_cellStamp[at] = m_generation;
        m_cells[at] = direct().cell(pos);
    }
    return m_cells[at];
}

void QcPathWindow::open(int index, float cost, int parent)
{
    const std::size_t at = static_cast<std::size_t>(index);
    m_nodeStamp[at] = m_generation;
    m_cost[at] = cost;
    m_parent[at] = parent;
    m_closed[at] = 0;
}
