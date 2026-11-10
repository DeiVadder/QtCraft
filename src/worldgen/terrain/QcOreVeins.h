// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <QVarLengthArray>

/*! \brief Veins of ore inside one chunk, for the terrain and the nether generator. */
namespace QcOreVeins {

/*! The cells of one vein, chunk-local; on the stack up to the largest vein the content allows
    (\c {veinSize} at most 64, QcWorldgenParser). */
using Vein = QVarLengthArray<QcBlockPos, 64>;

/*! Returns vein number \a vein of \a ore in the chunk at \a pos: a compact clump grown from its
    first cell, each block on a free face of an earlier one, kept inside the chunk, so veins never
    depend on neighbours. */
[[nodiscard]] Vein grown(const QcTerrainDefinitions::Ore &ore, int vein, QcChunkPos pos);
/*! Sets the cells of \a cells in \a chunk that hold the block \a ore replaces to the ore; a
    hidden ore leaves out the cells with air on a face (only faces inside the chunk count, so
    veins never depend on neighbours). */
void place(QcChunk &chunk, const QcTerrainDefinitions::Ore &ore, const Vein &cells);

} // namespace QcOreVeins
