// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief Day and night as a pure function of world time (seconds).

    Phase 0 is sunrise, 0.25 noon, 0.5 sunset and 0.75 midnight. */
namespace QcDayCycle {

/*! Seconds of a full day. */
inline constexpr double dayLength = 600.0;
/*! New worlds start in the morning, once the sky is fully bright. */
inline constexpr double startTime = dayLength * 0.1;
/*! The lowest brightness factor of sky light (at night). */
inline constexpr double minimumDaylight = 0.15;
/*! Levels sky light loses at night (midnight in clear weather). */
inline constexpr int nightDarkening = 11;

/*! The time of day a dimension without day cycle stays at, in seconds: midnight (decision
    E-1050). */
inline constexpr double midnightTime = dayLength * 0.75;

/*! Returns the phase of the day at \a worldTime, in [0, 1). */
double phase(double worldTime);
/*! Returns the height of the sun at \a worldTime, in [-1, 1]. */
double sunHeight(double worldTime);
/*! Returns the brightness factor for sky light at \a worldTime, in [minimumDaylight, 1]. */
double daylight(double worldTime);
/*! Returns the angle of the sun at \a worldTime in radians, in [0, 2 pi): 0 at noon, pi / 2 at
    sunset, pi at midnight, 3 pi / 2 at sunrise. */
double sunAngle(double worldTime);
/*! Returns the levels sky light loses at \a worldTime with \a rain and \a thunder of strengths
    0..1: 0 at noon in clear weather and nightDarkening at midnight; rain and thunder each take up
    to five sixteenths of what the sun gives (decision E-982). */
int skyDarkening(double worldTime, float rain, float thunder);
/*! Returns whether it is night at \a worldTime (the sun more than a little below the horizon). */
bool isNight(double worldTime);

} // namespace QcDayCycle
