// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcDials.h"

#include "common/world/QcDayCycle.h"

#include <cmath>
#include <numbers>

namespace QcDials {

namespace {

// `degrees` in [-180, 180).
double wrapped(double degrees)
{
    const double turn = std::fmod(degrees + 180.0, 360.0);
    return (turn < 0.0 ? turn + 360.0 : turn) - 180.0;
}

} // namespace

double compassAngle(const QcVec3 &position, double yaw, int spawnX, int spawnZ)
{
    const double dx = spawnX + 0.5 - position.x;
    const double dz = spawnZ + 0.5 - position.z;
    if (dx * dx + dz * dz < 0.25) {
        return 0.0;
    }

    // North is -z, east +x: the bearing of the spawn like a yaw.
    const double bearing = std::atan2(dx, -dz) * 180.0 / std::numbers::pi;
    return wrapped(bearing - yaw);
}

double clockAngle(double worldTime)
{
    // Phase 0.25 is noon (QcDayCycle).
    return std::fmod((QcDayCycle::phase(worldTime) - 0.25) * 360.0 + 360.0, 360.0);
}

double spinAngle(double seconds, Dial dial)
{
    // Two waves of different speed against a steady turn: the needle turns, hesitates and swings
    // back now and then, never the same way twice in a short while.
    const double phase = dial == Dial::Compass ? 0.0 : 2.1;
    const double angle = seconds * 140.0 + 160.0 * std::sin(seconds * 1.7 + phase) + 90.0 * std::sin(seconds * 0.6 + 2.0 * phase);
    const double turn = std::fmod(angle, 360.0);
    return turn < 0.0 ? turn + 360.0 : turn;
}

ClockTime clockTime(double worldTime)
{
    // Phase 0 is sunrise, shown as 6:00.
    const int minutes = static_cast<int>(std::floor(QcDayCycle::phase(worldTime) * 24.0 * 60.0)) + 6 * 60;
    const int ofDay = minutes % (24 * 60);
    return {ofDay / 60, ofDay % 60};
}

} // namespace QcDials
