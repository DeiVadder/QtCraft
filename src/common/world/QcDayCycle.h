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

/*! Returns the phase of the day at \a worldTime, in [0, 1). */
double phase(double worldTime);
/*! Returns the height of the sun at \a worldTime, in [-1, 1]. */
double sunHeight(double worldTime);
/*! Returns the brightness factor for sky light at \a worldTime, in [minimumDaylight, 1]. */
double daylight(double worldTime);
/*! Returns whether it is night at \a worldTime (the sun more than a little below the horizon). */
bool isNight(double worldTime);

} // namespace QcDayCycle
