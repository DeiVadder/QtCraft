// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPhysics.h"

/*! \brief The camera's height above the player's feet: it drops to QcPhysics::sneakEyeHeight
    while the player crouches and rises back when he stands up, at a steady speed over
    transitionSeconds, like the role model (decision E-961).

    Only the view eases; the aim takes the new height at once (QcPrediction::eyeHeight). */
class QcEyeHeight
{
public:
    /*! Seconds the camera takes from standing to crouching height or back. */
    static constexpr double transitionSeconds = 0.1;

    /*! Moves the height towards \a target (metres above the feet) by \a seconds of the
        transition and returns it. A first call, or a negative \a seconds, jumps to \a target. */
    double advance(double target, double seconds);
    /*! Returns the current height above the feet. */
    [[nodiscard]] double height() const { return m_height; }

private:
    void setHeight(double height) { m_height = height; }

    double m_height = -1.0;
};
