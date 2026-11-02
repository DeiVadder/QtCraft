// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief The time of the animated block textures in game ticks (decision E-878), advanced every
    frame by the frame's seconds at the server's speed (QcMsg::WorldTime::speed): smooth between
    ticks, faster in a /tick sprint, and standing while the server stands (pause menu, an empty
    server, /tick freeze; speed 0).

    It stands where it was and goes on from there, so resuming does not jump. */
class QcAnimationClock
{
public:
    /*! One frame of \a seconds (negative: none) at \a speed server ticks per normal tick
        (negative: 0). */
    void advance(double seconds, double speed);
    /*! Returns the time in ticks since the clock started, with fractions of a tick. */
    double ticks() const { return m_ticks; }

private:
    double m_ticks = 0.0;
};
