// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/explosions/QcBlastField.h"

#include "common/world/QcBlockAccess.h"

QcBlastField::QcBlastField(const QcBlockAccess &world, const QcBlastTable &table, QcBlockPos centre, int radius)
    : m_world(world)
    , m_table(table)
    , m_origin(centre.offset(-radius, -radius, -radius))
    , m_size(2 * radius + 1)
{
    const auto cells = static_cast<std::size_t>(m_size) * static_cast<std::size_t>(m_size) * static_cast<std::size_t>(m_size);
    m_cells.resize(cells);
    m_known.resize(cells, false);
}

QcBlastField::Cell QcBlastField::lookUp(QcBlockPos pos)
{
    ++m_lookups;
    Cell cell;
    if (!pos.isInsideWorldHeight()) {
        // Above the world is air, below it nothing a blast reaches into.
        cell.loaded = pos.y >= 0;
        return cell;
    }
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    if (!block) {
        cell.loaded = false;
        return cell;
    }
    const QcBlastTable::Entry &entry = m_table.of(*block);
    cell.block = *block;
    cell.resistance = entry.resistance;
    cell.solid = entry.solid;
    return cell;
}

const QcBlastField::Cell &QcBlastField::at(QcBlockPos pos)
{
    const int x = pos.x - m_origin.x;
    const int y = pos.y - m_origin.y;
    const int z = pos.z - m_origin.z;
    if (x < 0 || y < 0 || z < 0 || x >= m_size || y >= m_size || z >= m_size) {
        m_outside = lookUp(pos);
        return m_outside;
    }

    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_size)
        + static_cast<std::size_t>(z)) * static_cast<std::size_t>(m_size) + static_cast<std::size_t>(x);
    if (!m_known[index]) {
        m_cells[index] = lookUp(pos);
        m_known[index] = true;
    }
    return m_cells[index];
}
