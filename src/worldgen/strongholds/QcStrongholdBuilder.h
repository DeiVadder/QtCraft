// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStrongholdDefinition.h"
#include "common/world/QcChunk.h"
#include "worldgen/strongholds/QcStrongholdBlocks.h"
#include "worldgen/strongholds/QcStrongholdPlan.h"

class QcStrongholdCanvas;

/*! \brief Writes the cells of a stronghold that lie in one chunk (decision E-1171), the same in
    any order of the chunks: each piece fills its box with bricks, carves its air, opens its
    doorways and gets its stairs and furnishing; the pieces in the order of the plan. */
class QcStrongholdBuilder
{
public:
    /*! Builds with \a blocks and \a settings, which outlive it; \a seed picks the worn
        bricks and the cobwebs. */
    QcStrongholdBuilder(const QcStrongholdBlocks &blocks, const QcStrongholdSettings &settings, quint64 seed);

    /*! Writes the cells of \a plan that lie in \a chunk into it. */
    void build(QcChunk &chunk, const QcStrongholdPlan &plan) const;

private:
    // The walls, air, doorways, stairs and insides of `piece`.
    static void buildPiece(QcStrongholdCanvas &canvas, const QcStrongholdPlan::Piece &piece);
    // The doorway into `piece` with its door.
    static void doorway(QcStrongholdCanvas &canvas, const QcStrongholdPlan::Piece &piece);
    // The doorways out of `piece` a further piece leads on from.
    static void exits(QcStrongholdCanvas &canvas, const QcStrongholdPlan::Piece &piece);
    // The steps of a spiral staircase and of a straight flight of stairs.
    static void spiral(QcStrongholdCanvas &canvas, const QcStrongholdPlan::Piece &piece);
    static void flight(QcStrongholdCanvas &canvas, const QcStrongholdPlan::Piece &piece);

    const QcStrongholdBlocks &m_blocks;
    const QcStrongholdSettings &m_settings;
    quint64 m_seed = 0;
};
