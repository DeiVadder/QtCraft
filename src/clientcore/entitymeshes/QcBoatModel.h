// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>

/*! \brief The look of a boat (decision E-920): an open hull of planks with an oarlock on each side
    wall, built of boxes like the other static models, pointing north (-z) and turned by the boat's
    yaw, and two paddles turning in the oarlocks.

    Its planks are those of the block its item names (QcItemDefinition::boatBlock), so every kind
    of wood gives its boat and its paddles their colour without textures of their own. A floating
    boat bobs a little on the water. While the boat is rowed the paddles row in strokes
    (\c {rowPhase} 0..1); otherwise they rest along the sides with their blades in the water. The
    driver's hands hold the grips (QcAnimator: inBoat). */
namespace QcBoatModel {

/*! Amplitude (m) and period (s) of the bobbing of a floating boat. \qcshared */
inline constexpr double bobHeight = 0.025;
inline constexpr double bobPeriod = 3.0;
/*! Height of the side walls above the boat's feet; the oarlocks sit on them. */
inline constexpr double wallHeight = 0.5;
/*! Seconds per paddle stroke while rowing. */
inline constexpr double strokeSeconds = 0.9;

/*! \brief How a paddle lies in its oarlock, in degrees: \c {sweep} turns its outer end towards
    the stern (negative towards the bow), \c {lift} tilts its outer end down towards the water.
    \qcfields */
struct Paddle
{
    double sweep = 0.0;
    double lift = 0.0;
};

/*! Returns the boxes of a boat built of planks with the texture \a planks, without the paddles. */
[[nodiscard]] QList<QcMobPart> parts(const QString &planks);
/*! Returns the boxes of the paddle on \a side (+1 right, -1 left) in its own frame: the oarlock
    at the origin, the shaft along +x (the left paddle's mirrored along -x), the grip inside the
    boat, the blade outside. Placed with partPoint; textured with \a planks. */
[[nodiscard]] QList<QcMobPart> paddleParts(const QString &planks, int side);
/*! Returns the paddle at the moment \a rowPhase (0..1) of a stroke; negative: at rest. Both
    paddles move alike. */
[[nodiscard]] Paddle paddleAt(double rowPhase);
/*! Returns the point \a local of a paddle's frame (outer end along +x) in the boat's frame, for
    the paddle on \a side (+1 right, -1 left) lying as \a paddle. */
[[nodiscard]] QcVec3 paddlePoint(int side, Paddle paddle, const QcVec3 &local);
/*! Returns the \a corner of a box of paddleParts(…, \a side) in the boat's frame, the paddle
    lying as \a paddle. */
[[nodiscard]] QcVec3 partPoint(int side, Paddle paddle, const QcVec3 &corner);
/*! Returns where the driver's hand holds the paddle on \a side, lying as \a paddle, in the
    boat's frame. */
[[nodiscard]] QcVec3 gripOf(int side, Paddle paddle);
/*! Returns the phase of the stroke at \a seconds of the client clock while the boat is rowed (\a
    rowing); -1 when not. */
[[nodiscard]] double rowPhase(double seconds, bool rowing);
/*! Returns the texture of the planks of the boat item \a item: the side of its boatBlock; empty
    for items \a content does not know or that place no boat. */
[[nodiscard]] QString planksOf(const QcContentRegistry &content, QcItemId item);
/*! Returns how far a floating boat lies above or below its place at \a seconds of the client
    clock; each boat (by \a id) in its own phase. */
[[nodiscard]] double bob(double seconds, quint32 id);

} // namespace QcBoatModel
