// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtMath>

#include <cmath>

// The body of a boat (decision E-920), the same for the server (collisions, riders, placing) and the
// client (targeting, drawing, the driver's prediction). Lengths in metres, from the boat's feet (the
// middle of its bottom face); the box is square like every body box, its size the role model's.
// The model points along the boat's yaw (yaw 0: north, -z).
namespace QcBoatBody {

inline constexpr double width = 1.375;
inline constexpr double height = 0.5625;
// The top of the boat's floor above its feet: the riders sit on it.
inline constexpr double floor = 0.09;
// The hips of the player figure lie this high above its feet (the humanoid model's hip pivot); the
// driver sits with them `seatHeight` above the floor, low in the boat like in the role model, the
// thighs forward and level with the floor (half sunk into it, as in the role model).
inline constexpr double hipHeight = 0.7;
inline constexpr double seatHeight = 0.03;
// Where the driver and the passenger creature sit along the boat (forward positive): the driver in
// the back half (his legs reach to the middle of the front half), the creature in the front half,
// both always there whether the other seat is taken or not (the client predicts the driver's seat
// without knowing the passenger).
inline constexpr double driverAlong = -0.3;
inline constexpr double passengerAlong = 0.4;

// The horizontal direction the boat points to at `yaw` degrees (0: north, 90: east).
[[nodiscard]] inline QcVec3 forward(double yaw)
{
    const double radians = qDegreesToRadians(yaw);
    return {std::sin(radians), 0.0, -std::cos(radians)};
}

// The feet of the driver of a boat with its feet at `feet`, pointing to `yaw`.
[[nodiscard]] inline QcVec3 driverSeat(const QcVec3 &feet, double yaw)
{
    return feet + forward(yaw) * driverAlong + QcVec3{0.0, floor + seatHeight - hipHeight, 0.0};
}

// The feet of a creature sitting in the front of the boat: on its floor.
[[nodiscard]] inline QcVec3 passengerSeat(const QcVec3 &feet, double yaw)
{
    return feet + forward(yaw) * passengerAlong + QcVec3{0.0, floor, 0.0};
}

} // namespace QcBoatBody
