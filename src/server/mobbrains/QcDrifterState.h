// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

/*! \brief What a drifter (QcMobDefinition::Ai::Drifter, decision E-1053) keeps between ticks:
    where it heads, for how much longer, how long it has charged its shot, and whether it began a
    charge whose warning (QcArcherDefinition::warnSound) the server has yet to play. Not saved.
    \qcfields */
struct QcDrifterState
{
    QcVec3 heading;
    double headingSeconds = 0.0;
    double chargeSeconds = 0.0;
    bool warns = false;
};
