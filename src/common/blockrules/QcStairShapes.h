// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcBlockAccess.h"

/*! \brief Corners of stairs as in the reference (decision E-754).

    A stairs block has the states \c {facing} (where it climbs to), \c {half} and \c {shape} with
    the values straight, inner_left, inner_right, outer_left and outer_right
    (QcBlockDefinition::stairs). A stair takes its shape from the stairs of the same half in
    front of it (where it climbs to) and behind it: one in front turned across makes an outer
    corner, one behind turned across an inner corner, left or right as seen climbing up. A stair
    in a straight run (the neighbour on that side is the same stair) stays straight. The shape
    depends only on where stairs lie and how they face, never on their shapes, so the updates
    settle after one pass. */
namespace QcStairShapes {

/*! Returns \a id at \a pos with the shape the stairs around it in \a world give it; unchanged for
    blocks that are no stairs and while a neighbour that decides the shape is not loaded. */
[[nodiscard]] QcBlockId shapedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

} // namespace QcStairShapes
