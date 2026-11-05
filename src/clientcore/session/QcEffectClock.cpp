// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcEffectClock.h"

#include "common/physics/QcPhysics.h"

#include <algorithm>

void QcEffectClock::tick(double speed)
{
    // Never faster than real time: a sprint runs thousands of ticks a second.
    m_previous = m_time;
    m_time += QcPhysics::tickDuration * std::clamp(speed, 0.0, 1.0);
}

double QcEffectClock::seconds(double alpha) const
{
    return m_previous + std::clamp(alpha, 0.0, 1.0) * (m_time - m_previous);
}
