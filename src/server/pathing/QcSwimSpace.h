// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/physics/QcPhysics.h"

class QcBlockAccess;

/*! \brief Room for a swimming body in the water (decisions E-502, E-972): where a
    creature that lives in water fits with its whole box, and which moves keep it there. Shared by
    QcSwimRoutes and the swimmers' steering (QcFishSwimming). */
namespace QcSwimSpace {

/*! Distance between the points a straight line is tested at, in metres. */
inline constexpr double lineStep = 0.25;
/*! How far beyond a move a body tests for room, in metres, so that it stops a hair before the
    edge of the water instead of on it. */
inline constexpr double edgeMargin = 0.02;

/*! Returns the feet of a body of \a body resting in the cell \a cell: in the middle of the cell
    horizontally, its centre at the cell's middle height but its feet not below the cell's
    floor. */
[[nodiscard]] QcVec3 pointIn(QcBlockPos cell, QcPhysics::BodySize body);
/*! Returns whether a body of \a body with its feet at \a feet lies wholly in the water of
    \a world: every cell its box overlaps holds water, and the top of the box stays below the
    surface of the cells it reaches into (QcBoatMotion::waterSurfaceAt). */
[[nodiscard]] bool fits(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize body);
/*! Returns whether a body of \a body fits in the water of \a world all along the straight line
    from \a from to \a to, tested every lineStep (\a from itself is not tested). */
[[nodiscard]] bool clearLine(const QcBlockAccess &world, const QcVec3 &from, const QcVec3 &to, QcPhysics::BodySize body);
/*! Returns \a move without the components along which a body of \a body with its feet at \a feet
    would leave the water of \a world: each axis is tested on its own, the body shifted by the
    component times \a reach plus edgeMargin, so a body sliding along a wall keeps the component
    along the wall. Where the components each fit but the whole move does not (a corner of air
    or rock across the way), only the largest component is kept. The body must fit where it is
    (fits). */
[[nodiscard]] QcVec3 keepInside(const QcVec3 &move, const QcVec3 &feet, QcPhysics::BodySize body, const QcBlockAccess &world, double reach);

} // namespace QcSwimSpace
