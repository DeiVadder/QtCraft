// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief Progress of the own player's hand animations in one frame (QcClientSession::handTimers):
    switching the held item, landing and using; 0..1 while running, negative otherwise.

    The swing's progress is the pose's (QcEntityPose::swingProgress); \c {mining} says whether
    the running swing is the full loop of mining or the short swing of a single click, as it was
    when the swing started (decision E-881). */
struct QcHandTimers
{
    /*! Lengths of the animations in ticks. \qcshared */
    static constexpr int useTicks = 5;
    static constexpr int switchTicks = 6;
    static constexpr int landingTicks = 6;

    /*! Progress of switching the held item. */
    double switchProgress = -1.0;
    /*! Progress of the landing. */
    double landing = -1.0;
    /*! Progress of the use animation. */
    double use = -1.0;
    /*! Whether the running swing is the full loop of mining (else a click's short swing). */
    bool mining = false;
};
