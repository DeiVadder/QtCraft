// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/fortress/QcFortressBlocks.h"
#include "worldgen/fortress/QcFortressCanvas.h"
#include "worldgen/fortress/QcFortressPlan.h"
#include "worldgen/nether/QcNetherDefinitions.h"

/*! \brief Writes the cells of a fortress that lie in one chunk (decision E-1072), the same in any
    order of the chunks: it reads only the chunk's own blocks, in this order: pools beside the
    fortress sealed (QcNetherDefinitions::sealPools), all shells, all air, passage details (with
    windows and side openings where a gallery's wall stands in open air), rooms, chests and then
    the piers, which end on the first solid cell below. */
class QcFortressBuilder
{
public:
    /*! Builds with \a blocks and \a definitions, which outlive it; \a seed picks the worn
        bricks. */
    QcFortressBuilder(const QcFortressBlocks &blocks, const QcNetherDefinitions &definitions, quint64 seed);

    /*! Writes the cells of \a plan that lie in \a chunk. */
    void build(QcChunk &chunk, const QcFortressPlan &plan) const;

private:
    void seal(QcChunk &chunk, const QcFortressPlan &plan) const;
    static void buildShell(QcFortressCanvas &canvas, const QcFortressPlan::Room &room);
    static void buildShell(QcFortressCanvas &canvas, const QcFortressPlan::Passage &passage);
    static void carve(QcFortressCanvas &canvas, const QcFortressPlan::Room &room);
    static void carve(QcFortressCanvas &canvas, const QcFortressPlan::Passage &passage);
    static void dress(QcFortressCanvas &canvas, const QcFortressPlan::Passage &passage);
    static void dressOpenCell(QcFortressCanvas &canvas, const QcFortressPlan::Passage &passage, int along);
    static void dressClosedCell(QcFortressCanvas &canvas, const QcFortressPlan::Passage &passage, int along);
    // The walls of the cell `along` a closed passage opened over the sill with `fill` (a fence
    // or air) where open air stands behind them.
    static void cutOpening(QcFortressCanvas &canvas, const QcFortressPlan::Passage &passage, int along, QcBlockId fill);
    static void placePiers(QcFortressCanvas &canvas, const QcFortressPlan &plan);

    const QcFortressBlocks &m_blocks;
    const QcNetherDefinitions &m_definitions;
    quint64 m_seed = 0;
};
