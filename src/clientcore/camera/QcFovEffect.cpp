// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcFovEffect.h"

#include "common/content/items/QcProjectileDefinition.h"

#include <algorithm>
#include <cmath>

double QcFovEffect::drawShare(double drawProgress)
{
    return drawProgress >= 0.0 ? QcLauncherDefinition::shareOf(drawProgress) : 0.0;
}

void QcFovEffect::setShare(double share)
{
    m_share = std::isfinite(share) ? std::clamp(share, 0.0, 1.0) : 1.0;
}

double QcFovEffect::target(Movement movement) const
{
    const double widening = (movement.sprinting ? sprintWidening : 0.0) + (movement.flying ? flyWidening : 0.0);
    const double draw = std::isfinite(movement.draw) ? std::clamp(movement.draw, 0.0, 1.0) : 0.0;
    const double narrowing = drawNarrowing * draw * draw;
    return (1.0 + widening * m_share) * (1.0 - narrowing * m_share);
}

double QcFovEffect::advance(Movement movement, double seconds)
{
    const double goal = target(movement);
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return m_factor;
    }

    // Exponential easing: the same curve at every frame rate.
    m_factor = goal + (m_factor - goal) * std::exp(-seconds / easeSeconds);
    if (std::abs(m_factor - goal) < 1e-4) {
        m_factor = goal;
    }
    return m_factor;
}
