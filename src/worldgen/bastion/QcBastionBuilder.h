// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/bastion/QcBastionBlocks.h"
#include "worldgen/bastion/QcBastionPlan.h"
#include "worldgen/generators/QcChunkCanvas.h"
#include "worldgen/nether/QcNetherDefinitions.h"

/*! \brief Writes the cells of a bastion that lie in one chunk (decision E-1087), the same in any
    order of the chunks: it reads only the chunk's own blocks, in this order: pools around its parts
    sealed (QcNetherDefinitions::sealPools), the strokes of the plan, the chests and the
    spawners. */
class QcBastionBuilder
{
public:
    /*! Builds with \a blocks and \a definitions, which outlive it; \a seed picks the worn bricks
        and what falls away. */
    QcBastionBuilder(const QcBastionBlocks &blocks, const QcNetherDefinitions &definitions, quint64 seed);

    /*! Writes the cells of \a plan that lie in \a chunk. */
    void build(QcChunk &chunk, const QcBastionPlan &plan) const;

private:
    [[nodiscard]] const QcBastionBlocks &blocks() const { return m_blocks; }
    // The block a stroke paints, for those that give one block to every cell.
    [[nodiscard]] QcBlockId blockOf(const QcBastionPlan::Stroke &stroke) const;
    void paint(QcChunkCanvas &canvas, const QcBastionPlan::Stroke &stroke) const;
    void brick(QcChunkCanvas &canvas, const QcBlockBox &box) const;
    void foundation(QcChunkCanvas &canvas, const QcBlockBox &box) const;
    void crumble(QcChunkCanvas &canvas, const QcBlockBox &box) const;
    // Takes the cells of `row` away, each with the chance `share`.
    void crumbleRow(QcChunkCanvas &canvas, const QcBlockBox &row, double share) const;

    const QcBastionBlocks &m_blocks;
    const QcNetherDefinitions &m_definitions;
    quint64 m_seed = 0;
};
