// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <optional>

/*! \brief What a drifter (QcMobDefinition::Ai::Drifter, decision E-1053) keeps between ticks:
    where it heads, for how much longer (\c {resting}: it rests for that long instead), how long it
    has charged its shot, whether it began a charge whose warning (QcArcherDefinition::warnSound)
    the server has yet to play, how many shots of the volley it fired and whether it shows flames
    for it (QcArcherDefinition::flares), and for one that rises to its target (decision E-1056) the
    height its feet keep, how far below the target's eyes its own stay and for how much longer.
    Not saved. \qcfields */
struct QcDrifterState
{
    QcVec3 heading;
    double headingSeconds = 0.0;
    bool resting = false;
    double chargeSeconds = 0.0;
    bool warns = false;
    int shotsFired = 0;
    bool flares = false;
    std::optional<double> feetY;
    double heightOffset = 0.0;
    double offsetSeconds = 0.0;

    /*! Ends the charge and its volley. */
    void stopCharge()
    {
        chargeSeconds = 0.0;
        shotsFired = 0;
        flares = false;
    }
};
