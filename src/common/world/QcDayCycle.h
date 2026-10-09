#pragma once

// Day and night as a pure function of world time (seconds). Phase 0 is sunrise, 0.25 noon,
// 0.5 sunset and 0.75 midnight.
namespace QcDayCycle {

inline constexpr double dayLength = 600.0;
// New worlds start in the morning, once the sky is fully bright.
inline constexpr double startTime = dayLength * 0.1;
inline constexpr double minimumDaylight = 0.15;

double phase(double worldTime);
// Height of the sun in [-1, 1].
double sunHeight(double worldTime);
// Brightness factor for sky light in [minimumDaylight, 1].
double daylight(double worldTime);
bool isNight(double worldTime);

} // namespace QcDayCycle
