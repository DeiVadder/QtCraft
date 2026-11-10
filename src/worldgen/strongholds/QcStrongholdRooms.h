// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/strongholds/QcStrongholdCanvas.h"
#include "worldgen/strongholds/QcStrongholdPlan.h"

#include <optional>

/*! \brief The insides of the pieces of a stronghold (decision E-1172): what stands in a piece
    once its walls are built and its air carved out, and where its chests, spawner and frames
    lie (the planner and the builder read the same places). */
namespace QcStrongholdRooms {

/*! Returns the chests of \a piece. */
[[nodiscard]] QList<QcStrongholdPlan::Chest> chestsOf(const QcStrongholdPlan::Piece &piece);
/*! Returns the spawner of \a piece; none but in the portal room. */
[[nodiscard]] std::optional<QcBlockPos> spawnerOf(const QcStrongholdPlan::Piece &piece);
/*! Returns the end portal frames of \a piece, each with an eye at a draw from \a seed and its
    cell below \a eyeChance; none but in the portal room. */
[[nodiscard]] QList<QcStrongholdPlan::Frame> framesOf(const QcStrongholdPlan::Piece &piece, quint64 seed, double eyeChance);
/*! Writes the insides of \a piece with \a canvas: cells, fountains, store rooms, shelves,
    stairs and lava; its chests and its spawner, not its frames. */
void furnish(QcStrongholdCanvas &canvas, const QcStrongholdPlan::Piece &piece);

} // namespace QcStrongholdRooms
