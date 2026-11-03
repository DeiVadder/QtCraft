// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcEyeHeight.h"

#include <algorithm>

double QcEyeHeight::advance(double target, double seconds)
{
    // Not placed yet (a new session) or no time to ease: at the target at once.
    const double now = height();
    if (now < 0.0 || seconds < 0.0) {
        setHeight(target);
        return target;
    }

    constexpr double speed = (QcPhysics::playerEyeHeight - QcPhysics::sneakEyeHeight) / transitionSeconds;
    const double step = speed * seconds;
    setHeight(std::clamp(target, now - step, now + step));
    return height();
}
