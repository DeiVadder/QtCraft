// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief Progress of the own player's hand animations in one frame (QcClientSession::handTimers):
    switching the held item, landing and using; 0..1 while running, negative otherwise. \c {aim}
    is how far the hand has come to a drawn launcher (decision E-984).

    The swing's progress is the pose's (QcEntityPose::swingProgress); \c {mining} says whether
    the running swing is the full loop of mining or the short swing of a single click, as it was
    when the swing started (decision E-881). */
struct QcHandTimers
{
    /*! Lengths of the animations in ticks. \qcshared */
    static constexpr int useTicks = 5;
    static constexpr int switchTicks = 6;
    static constexpr int landingTicks = 6;
    static constexpr int aimTicks = 3;

    /*! Progress of switching the held item. */
    double switchProgress = -1.0;
    /*! Progress of the landing. */
    double landing = -1.0;
    /*! Progress of the use animation. */
    double use = -1.0;
    /*! Whether the running swing is the full loop of mining (else a click's short swing). */
    bool mining = false;
    /*! How far the hand has come to the drawn launcher, 0..1: it rises over aimTicks once the
        draw begins and falls as fast after the shot or when the draw is let go. */
    double aim = 0.0;
    /*! The draw the hand shows at least (0..1): the last draw's power, so it stays where it was
        after the shot until the aim has fallen, and fades within aimTicks when a new, weaker
        draw begins while the aim still falls. */
    double lastDraw = 0.0;
};
