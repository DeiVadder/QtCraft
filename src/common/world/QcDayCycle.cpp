// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace QcDayCycle {

double phase(double worldTime)
{
    const double value = std::fmod(worldTime, dayLength) / dayLength;
    return value < 0.0 ? value + 1.0 : value;
}

double sunHeight(double worldTime)
{
    return std::sin(2.0 * std::numbers::pi * phase(worldTime));
}

double daylight(double worldTime)
{
    // Steep around sunrise and sunset so day and night each look settled most of the time.
    return std::clamp(0.35 + sunHeight(worldTime) * 1.3, minimumDaylight, 1.0);
}

bool isNight(double worldTime)
{
    return sunHeight(worldTime) < -0.1;
}

} // namespace QcDayCycle
