#include "QcSky.h"

#include "QcDayCycle.h"
#include "QcWorldConstants.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace QcSky {

namespace {

// Colours of the sky by day, at night and at sunrise or sunset.
const QVector3D dayZenith(0.25f, 0.48f, 0.86f);
const QVector3D dayHorizon(0.66f, 0.80f, 0.95f);
const QVector3D nightZenith(0.010f, 0.014f, 0.045f);
const QVector3D nightHorizon(0.035f, 0.050f, 0.105f);
const QVector3D duskHorizon(0.93f, 0.52f, 0.30f);
const QVector3D duskGlow(1.00f, 0.45f, 0.15f);
const QVector3D dayClouds(0.97f, 0.97f, 0.98f);
const QVector3D nightClouds(0.10f, 0.11f, 0.15f);
// The sun's path is tilted a little towards the south, so it never stands exactly overhead.
constexpr double pathTilt = 0.25;

QVector3D mix(QVector3D a, QVector3D b, double t)
{
    return a + (b - a) * float(std::clamp(t, 0.0, 1.0));
}

double smooth(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Share of sunrise or sunset colours: highest while the sun touches the horizon.
double duskShare(double sunHeight)
{
    return std::max(0.0, 1.0 - std::abs(sunHeight + 0.05) / 0.3);
}

} // namespace

QcSkyState at(double worldTime)
{
    QcSkyState sky;
    const double angle = 2.0 * std::numbers::pi * QcDayCycle::phase(worldTime);
    sky.sunDirection = QVector3D(float(std::cos(angle)), float(std::sin(angle)), float(pathTilt)).normalized();
    sky.moonDirection = -QVector3D(sky.sunDirection.x(), sky.sunDirection.y(), -sky.sunDirection.z());
    sky.daylight = QcDayCycle::daylight(worldTime);

    // Day share follows the sun's height through a short twilight.
    const double sunHeight = std::sin(angle);
    const double day = smooth(-0.18, 0.25, sunHeight);
    const double dusk = duskShare(sunHeight);
    sky.zenith = mix(nightZenith, dayZenith, day);
    sky.horizon = mix(mix(nightHorizon, dayHorizon, day), duskHorizon, dusk * 0.85);
    sky.glow = duskGlow * float(dusk);
    sky.stars = 1.0 - smooth(-0.25, 0.02, sunHeight);
    sky.cloudColor = mix(mix(nightClouds, dayClouds, day), QVector3D(1.0f, 0.70f, 0.55f), dusk * 0.6);

    // One step per day (days begin at sunrise, so a night keeps its moon); the first night of a
    // world has a full moon.
    const double dayNumber = std::floor(worldTime / QcDayCycle::dayLength);
    const double phase = std::fmod(dayNumber / double(lunarDays) + 0.5, 1.0);
    sky.moonPhase = phase < 0.0 ? phase + 1.0 : phase;

    sky.cloudOffsetX = worldTime * cloudSpeed;
    sky.cloudOffsetZ = worldTime * cloudSpeed * 0.35;
    return sky;
}

QcSkyState withWeather(QcSkyState sky, double rain, double thunder, double flash)
{
    rain = std::clamp(rain, 0.0, 1.0);
    thunder = std::clamp(thunder, 0.0, 1.0);
    flash = std::clamp(flash, 0.0, 1.0);
    if (rain <= 0.0 && thunder <= 0.0 && flash <= 0.0) {
        return sky;
    }

    // Grey of the same brightness, darker with the rain and darker still in a thunderstorm.
    const auto overcastColour = [rain, thunder](QVector3D colour, double darkening) {
        const float grey = 0.3f * colour.x() + 0.59f * colour.y() + 0.11f * colour.z();
        const QVector3D target = QVector3D(grey, grey, grey * 1.06f) * float(darkening * (1.0 - 0.45 * thunder));
        return mix(colour, target, rain * 0.85);
    };
    sky.zenith = overcastColour(sky.zenith, 0.55);
    sky.horizon = overcastColour(sky.horizon, 0.72);
    sky.cloudColor = overcastColour(sky.cloudColor, 0.62);
    sky.glow *= float(1.0 - rain);
    sky.stars *= 1.0 - rain;
    sky.overcast = rain;
    sky.fogScale = 1.0 - 0.45 * rain - 0.15 * thunder;
    sky.daylight = std::max(QcDayCycle::minimumDaylight, sky.daylight * (1.0 - 0.3 * rain - 0.2 * thunder));

    // A flash lights the clouds and the land, most of all by night.
    const QVector3D flashColour(0.82f, 0.86f, 1.0f);
    sky.zenith = mix(sky.zenith, flashColour * 0.7f, flash * 0.6);
    sky.horizon = mix(sky.horizon, flashColour * 0.8f, flash * 0.6);
    sky.cloudColor = mix(sky.cloudColor, flashColour, flash * 0.7);
    sky.daylight = sky.daylight + (1.0 - sky.daylight) * flash * 0.75;
    return sky;
}

QcSkyState seenFrom(QcSkyState sky, double eyeY)
{
    if (eyeY >= 0.0) {
        return sky;
    }

    const float light = float(std::clamp(1.0 - eyeY / QcWorld::voidLimitY, 0.0, 1.0));
    sky.zenith *= light;
    sky.horizon *= light;
    sky.glow *= light;
    sky.cloudColor *= light;
    sky.stars *= light;
    return sky;
}

} // namespace QcSky
