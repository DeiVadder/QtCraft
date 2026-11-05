// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/explosions/QcBlastTable.h"

#include "common/core/QcBlockPos.h"

#include <vector>

class QcBlockAccess;

/*! \brief The blocks around one explosion as its rays see them (decision E-602), each cell looked
    up in the world once and then read from a flat cache: a blast's rays cross most cells several
    times (steps of 0.3 blocks), and the sight lines to bodies cross them again.

    Cells beyond \c {radius} of the centre are read from the world directly. A cell of a chunk
    that is not loaded is \c {loaded} false: it stops rays and sight and never breaks. */
class QcBlastField
{
public:
    /*! \brief A cell as the rays see it. */
    struct Cell
    {
        /*! The block in it. */
        QcBlockId block = qcAirBlockId;
        /*! How much of a blast's force it soaks up (QcBlastTable). */
        float resistance = 0.0f;
        /*! It stops the sight of a blast on bodies. */
        bool solid = false;
        /*! Its chunk is loaded. */
        bool loaded = true;
    };

    QcBlastField(const QcBlockAccess &world, const QcBlastTable &table, QcBlockPos centre, int radius);

    /*! Returns the cell at \a pos, looked up in the world the first time. */
    const Cell &at(QcBlockPos pos);
    /*! Returns the blast traits of the block states. */
    const QcBlastTable &table() const { return m_table; }
    /*! Returns the cells looked up in the world so far (measurements). */
    qsizetype lookups() const { return m_lookups; }

private:
    Cell lookUp(QcBlockPos pos);

    const QcBlockAccess &m_world;
    const QcBlastTable &m_table;
    QcBlockPos m_origin;
    int m_size = 0;
    std::vector<Cell> m_cells;
    std::vector<bool> m_known;
    Cell m_outside;
    qsizetype m_lookups = 0;
};
