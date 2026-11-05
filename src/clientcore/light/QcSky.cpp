// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcSky.h"

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcDayCycle.h"

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
// The nether's fog and the end's vault when the dimension names no fog colour (decision E-1003).
const QVector3D netherFog(0.20f, 0.03f, 0.03f);
const QVector3D endZenith(0.02f, 0.015f, 0.035f);
const QVector3D endHorizon(0.07f, 0.055f, 0.10f);
// The factor on the fog's distances in the nether.
constexpr double netherFogScale = 0.5;
// The share of the ambient light (0..15) on the shaders' floor at full ambient light.
constexpr double ambientShare = 0.6;
// The sun's path is tilted a little towards the south, so it never stands exactly overhead.
constexpr double pathTilt = 0.25;

QVector3D mix(QVector3D a, QVector3D b, double t)
{
    return a + (b - a) * static_cast<float>(std::clamp(t, 0.0, 1.0));
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

// The fog colour 0xRRGGBB as linear RGB in 0..1.
QVector3D colourOf(quint32 rgb)
{
    return QVector3D(static_cast<float>((rgb >> 16) & 0xFF), static_cast<float>((rgb >> 8) & 0xFF), static_cast<float>(rgb & 0xFF)) / 255.0f;
}

// A sky without sun, moon, stars and clouds: `horizon` all around, `zenith` above.
QcSkyState vault(QVector3D zenith, QVector3D horizon)
{
    QcSkyState sky;
    sky.zenith = zenith;
    sky.horizon = horizon;
    sky.cloudColor = horizon;
    // Below the horizon, so that nothing would light from there if anything still read them.
    sky.sunDirection = QVector3D(0.0f, -1.0f, 0.0f);
    sky.moonDirection = QVector3D(0.0f, -1.0f, 0.0f);
    sky.celestial = false;
    return sky;
}

} // namespace

QcSkyState forDimension(const QcDimensionDefinition &dimension, double worldTime)
{
    using Sky = QcDimensionDefinition::Sky;
    const std::optional<QVector3D> fog = dimension.fogColour ? std::optional<QVector3D>(colourOf(*dimension.fogColour)) : std::nullopt;
    QcSkyState sky;
    switch (dimension.sky) {
    case Sky::Overworld:
        sky = at(dimension.dayCycle ? worldTime : fixedNoon);
        sky.horizon = fog.value_or(sky.horizon);
        break;
    case Sky::Nether:
        sky = vault(fog.value_or(netherFog), fog.value_or(netherFog));
        // The nether's fog stands close, as the hot air hides the far caverns.
        sky.fogScale = netherFogScale;
        break;
    case Sky::End:
        sky = vault(endZenith, fog.value_or(endHorizon));
        break;
    }

    sky.skyLight = dimension.skyLight;
    sky.daylight = dimension.skyLight ? sky.daylight : 0.0;
    sky.flashLight = std::max(sky.flashLight, ambientShare * std::clamp(dimension.ambientLight, 0, 15) / 15.0);
    return sky;
}

QcSkyState at(double worldTime)
{
    QcSkyState sky;
    const double angle = 2.0 * std::numbers::pi * QcDayCycle::phase(worldTime);
    sky.sunDirection = QVector3D(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)), static_cast<float>(pathTilt)).normalized();
    sky.moonDirection = -QVector3D(sky.sunDirection.x(), sky.sunDirection.y(), -sky.sunDirection.z());
    sky.daylight = QcDayCycle::daylight(worldTime);

    // Day share follows the sun's height through a short twilight.
    const double sunHeight = std::sin(angle);
    const double day = smooth(-0.18, 0.25, sunHeight);
    const double dusk = duskShare(sunHeight);
    sky.zenith = mix(nightZenith, dayZenith, day);
    sky.horizon = mix(mix(nightHorizon, dayHorizon, day), duskHorizon, dusk * 0.85);
    sky.glow = duskGlow * static_cast<float>(dusk);
    sky.stars = 1.0 - smooth(-0.25, 0.02, sunHeight);
    sky.cloudColor = mix(mix(nightClouds, dayClouds, day), QVector3D(1.0f, 0.70f, 0.55f), dusk * 0.6);

    // One step per day (days begin at sunrise, so a night keeps its moon); the first night of a
    // world has a full moon.
    const double dayNumber = std::floor(worldTime / QcDayCycle::dayLength);
    const double phase = std::fmod(dayNumber / static_cast<double>(lunarDays) + 0.5, 1.0);
    sky.moonPhase = phase < 0.0 ? phase + 1.0 : phase;
    return sky;
}

QcSkyState withCloudDrift(QcSkyState sky, double seconds)
{
    sky.cloudOffsetX = seconds * cloudSpeed;
    sky.cloudOffsetZ = seconds * cloudSpeed * 0.35;
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
        const QVector3D target = QVector3D(grey, grey, grey * 1.06f) * static_cast<float>(darkening * (1.0 - 0.45 * thunder));
        return mix(colour, target, rain * 0.85);
    };
    sky.zenith = overcastColour(sky.zenith, 0.55);
    sky.horizon = overcastColour(sky.horizon, 0.72);
    sky.cloudColor = overcastColour(sky.cloudColor, 0.62);
    sky.glow *= static_cast<float>(1.0 - rain);
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

QcSkyState withBlast(QcSkyState sky, double flash)
{
    flash = std::clamp(flash, 0.0, 1.0);
    if (flash <= 0.0) {
        return sky;
    }

    const QVector3D fireColour(1.0f, 0.78f, 0.5f);
    sky.zenith = mix(sky.zenith, fireColour * 0.8f, flash * 0.35);
    sky.horizon = mix(sky.horizon, fireColour, flash * 0.5);
    sky.cloudColor = mix(sky.cloudColor, fireColour, flash * 0.5);
    sky.flashLight = std::max(sky.flashLight, flash * 0.6);
    return sky;
}

QcSkyState withNightVision(QcSkyState sky, bool sees)
{
    constexpr double nightVisionLight = 0.76;
    sky.flashLight = sees ? std::max(sky.flashLight, nightVisionLight) : sky.flashLight;
    return sky;
}

QcSkyState seenFrom(QcSkyState sky, double eyeY)
{
    if (eyeY >= 0.0) {
        return sky;
    }

    const float light = static_cast<float>(std::clamp(1.0 - eyeY / QcWorld::voidLimitY, 0.0, 1.0));
    sky.zenith *= light;
    sky.horizon *= light;
    sky.glow *= light;
    sky.cloudColor *= light;
    sky.stars *= light;
    return sky;
}

} // namespace QcSky
