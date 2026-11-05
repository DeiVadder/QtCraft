// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

/*! \brief What the dials of held items show (decision E-769), as pure functions of the session's
    state. */
namespace QcDials {

/*! Returns the compass needle: degrees clockwise from straight ahead (the top of the dial)
    towards the world spawn column (\a spawnX, \a spawnZ) for a player at \a position looking
    along \a yaw (degrees, 0 north, 90 east), in [-180, 180); 0 when the player stands on the
    spawn column. */
double compassAngle(const QcVec3 &position, double yaw, int spawnX, int spawnZ);

/*! Returns the clock at \a worldTime: the hand in degrees clockwise from the top, noon at the
    top and midnight at the bottom (one turn per day), in [0, 360). */
double clockAngle(double worldTime);
/*! Which dial spins (spinAngle()): the two do not turn alike. */
enum class Dial {
    /*! The compass's needle. */
    Compass,
    /*! The clock's hand. */
    Clock,
};

/*! Returns where the needle of a compass or the hand of a clock (\a dial) stands after \a seconds
    of the world's animation clock in a dimension where they spin
    (QcDimensionDefinition::compassesSpin, decision E-1004): degrees clockwise from the top in
    [0, 360), turning unevenly and now and then back, as a pure function of the time. */
double spinAngle(double seconds, Dial dial);

/*! \brief The time of day the clock shows as hours and minutes, sunrise at 6:00. */
struct ClockTime
{
    /*! Hours, 0..23. */
    int hours = 0;
    /*! Minutes, 0..59. */
    int minutes = 0;
};
/*! Returns the time of day the clock shows at \a worldTime. */
ClockTime clockTime(double worldTime);

} // namespace QcDials
