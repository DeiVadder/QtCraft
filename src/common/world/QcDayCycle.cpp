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

double sunAngle(double worldTime)
{
    return 2.0 * std::numbers::pi * phase(worldTime - 0.25 * dayLength);
}

int skyDarkening(double worldTime, float rain, float thunder)
{
    // The sun's share of the sky light, 1 at noon and 0 at night.
    double sun = (daylight(worldTime) - minimumDaylight) / (1.0 - minimumDaylight);
    sun *= 1.0 - std::clamp(static_cast<double>(rain), 0.0, 1.0) * 5.0 / 16.0;
    sun *= 1.0 - std::clamp(static_cast<double>(thunder), 0.0, 1.0) * 5.0 / 16.0;
    return std::clamp(static_cast<int>((1.0 - sun) * nightDarkening), 0, nightDarkening);
}

bool isNight(double worldTime)
{
    return sunHeight(worldTime) < -0.1;
}

} // namespace QcDayCycle
