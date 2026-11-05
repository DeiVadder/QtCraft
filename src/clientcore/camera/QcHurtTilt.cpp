// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcHurtTilt.h"

#include <QtMath>

#include <cmath>
#include <numbers>

double QcHurtTilt::strength(double progress)
{
    if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0) {
        return 0.0;
    }
    const double left = 1.0 - progress;
    return std::sin(left * left * left * left * std::numbers::pi);
}

QcHurtTilt::Tilt QcHurtTilt::at(double progress, std::optional<double> fromYaw)
{
    const double degrees = strength(progress) * maxDegrees;
    if (!fromYaw || !std::isfinite(*fromYaw)) {
        return {.pitch = 0.0, .roll = degrees};
    }
    // From the front the head is knocked back (the view tips up), from the right it leans left.
    const double angle = qDegreesToRadians(*fromYaw);
    return {.pitch = degrees * std::cos(angle), .roll = -degrees * std::sin(angle)};
}
