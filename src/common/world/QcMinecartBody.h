// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The body of a minecart (decision E-382), the same for the server (collisions, pistons, riders) and
// the client (targeting, drawing, the rider's view). Lengths in metres, from the cart's feet (the
// middle of its bottom face, on the rail).
namespace QcMinecartBody {

inline constexpr double width = 0.98;
inline constexpr double height = 0.7;
// A rider's feet lie this far below the cart's: the rider sits with the legs forward (the hips at
// the usual hip height above the feet), so the hips rest on the cart's floor.
inline constexpr double seatOffset = -0.35;

} // namespace QcMinecartBody
