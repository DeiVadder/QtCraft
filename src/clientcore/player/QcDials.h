// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

// What the dials of held items show (decision E-769), as pure functions of the session's state.
namespace QcDials {

// The compass needle: degrees clockwise from straight ahead (the top of the dial) towards the
// world spawn column (x, z) for a player at `position` looking along `yaw` (degrees, 0 north, 90
// east), in [-180, 180); 0 when the player stands on the spawn column.
double compassAngle(const QcVec3 &position, double yaw, int spawnX, int spawnZ);

// The clock: the hand in degrees clockwise from the top, noon at the top and midnight at the
// bottom (one turn per day), in [0, 360).
double clockAngle(double worldTime);
// The time of day the clock shows as hours and minutes, sunrise at 6:00.
struct ClockTime
{
    int hours = 0;
    int minutes = 0;
};
ClockTime clockTime(double worldTime);

} // namespace QcDials
