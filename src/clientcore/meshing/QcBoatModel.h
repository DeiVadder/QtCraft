// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcMobDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>

// The look of a boat (decision E-920): an open hull of planks with an oarlock on each side wall,
// built of boxes like the other static models, pointing north (-z) and turned by the boat's yaw,
// and two paddles turning in the oarlocks. Its planks are those of the block its item names
// (QcItemDefinition::boatBlock), so every kind of wood gives its boat and its paddles their colour
// without textures of their own. A floating boat bobs a little on the water. While the boat is
// rowed the paddles row in strokes (`rowPhase` 0..1); otherwise they rest along the sides with their
// blades in the water. The driver's hands hold the grips (QcAnimator: inBoat).
namespace QcBoatModel {

// Amplitude (m) and period (s) of the bobbing of a floating boat.
inline constexpr double bobHeight = 0.025;
inline constexpr double bobPeriod = 3.0;
// Height of the side walls above the boat's feet; the oarlocks sit on them.
inline constexpr double wallHeight = 0.5;
// Seconds per paddle stroke while rowing.
inline constexpr double strokeSeconds = 0.9;

// How a paddle lies in its oarlock, in degrees: `sweep` turns its outer end towards the stern
// (negative towards the bow), `lift` tilts its outer end down towards the water.
struct Paddle
{
    double sweep = 0.0;
    double lift = 0.0;
};

// The boxes of a boat built of planks with the texture `planks`, without the paddles.
[[nodiscard]] QList<QcMobPart> parts(const QString &planks);
// The boxes of the paddle on `side` (+1 right, -1 left) in its own frame: the oarlock at the
// origin, the shaft along +x (the left paddle's mirrored along -x), the grip inside the boat, the
// blade outside. Placed with partPoint.
[[nodiscard]] QList<QcMobPart> paddleParts(const QString &planks, int side);
// The paddle at the moment `rowPhase` (0..1) of a stroke; negative: at rest. Both paddles move
// alike.
[[nodiscard]] Paddle paddleAt(double rowPhase);
// A point `local` of a paddle's frame (outer end along +x) in the boat's frame, for the paddle on
// `side` (+1 right, -1 left) lying as `paddle`.
[[nodiscard]] QcVec3 paddlePoint(int side, Paddle paddle, const QcVec3 &local);
// A corner of a box of paddleParts(…, side) in the boat's frame.
[[nodiscard]] QcVec3 partPoint(int side, Paddle paddle, const QcVec3 &corner);
// Where the driver's hand holds the paddle on `side`, in the boat's frame.
[[nodiscard]] QcVec3 gripOf(int side, Paddle paddle);
// The phase of the stroke at `seconds` of the client clock while the boat is rowed; -1 when not.
[[nodiscard]] double rowPhase(double seconds, bool rowing);
// The texture of the planks of the boat item `item`: the side of its boatBlock; empty for items the
// content does not know or that place no boat.
[[nodiscard]] QString planksOf(const QcContentRegistry &content, QcItemId item);
// How far a floating boat lies above or below its place at `seconds` of the client clock; each boat
// (by `id`) in its own phase.
[[nodiscard]] double bob(double seconds, quint32 id);

} // namespace QcBoatModel
