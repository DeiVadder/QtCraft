// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"

#include <array>
#include <optional>

/*! \brief The heights of a liquid's surface at the four top corners of its cell (decision E-885),
    so the surface slopes towards where the liquid flows, as in the reference game: each corner
    averages the surfaces of the up to four cells sharing it, sources and full cells weighing ten
    times as much as lower flows, open cells (air, torches, plants, thin snow;
    QcBlockRenderTable::Entry:: liquidMayEnter) counting as height 0 and closed cells (solid blocks,
    other liquids) not at all; a corner next to a full cell (liquid on top of it) is full.

    Two cells of the same liquid compute the same heights for the corners they share, so their
    surfaces meet without a step. */
namespace QcLiquidSurface {

/*! The value of a cell beside the liquid that the liquid could flow into (air, a torch, a
    plant). */
inline constexpr float open = 0.0f;
/*! The value of a cell that holds neither the liquid nor room for it. */
inline constexpr float closed = -1.0f;
/*! Surfaces at least this high weigh \c {heavyWeight} times as much in the average. \qcshared */
inline constexpr float heavyFrom = 0.8f;
inline constexpr float heavyWeight = 10.0f;

/*! \a cells: the 3 x 3 cells around the liquid at its height, index (dz + 1) * 3 + (dx + 1), the
    liquid itself in the middle; each is the surface height of the same liquid there (0..1, 1
    when liquid lies on top of it), \c {open} or \c {closed}. Returns the corner heights, index x
    + 2 * z with x and z 0 or 1 within the cell. */
[[nodiscard]] std::array<float, 4> cornerHeights(const std::array<float, 9> &cells);

/*! Returns where the side face \a face of a translucent block (glass, ice) starts at its
    corners, index x + 2 * z within the block's cell, when the cell behind that face holds a
    translucent liquid whose top corners are \a liquidTop (index x + 2 * z within the liquid's
    cell): at the liquid's surface, so the liquid's side (0 up to its surface) and the block's
    side (from the surface up) share the boundary without overlapping (decision E-959). Corners
    off the shared edge are 0. Returns nullopt when both corners of the shared edge are full
    (1): the liquid's side covers the whole boundary. One full corner leaves a triangle. */
[[nodiscard]] std::optional<std::array<float, 4>> shoreBase(QcBlockFace face, std::array<float, 4> liquidTop);

} // namespace QcLiquidSurface
