// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcDayCycle.h"

#include <QVector3D>

struct QcDimensionDefinition;

/*! \brief Look of the sky at a moment of the day, as a pure function of the world time: colours of
    the dome, where sun and moon stand, the moon's phase and how visible the stars are; how far the
    clouds have drifted follows the world's animation clock instead (QcSky::withCloudDrift,
    decision E-968).

    The renderer draws the dome from it; fog takes the horizon colour so the far terrain fades
    into the sky. */
struct QcSkyState
{
    /*! Linear RGB in 0..1. \qcshared */
    QVector3D zenith;
    QVector3D horizon;
    /*! Warm glow around the sun near the horizon (sunrise, sunset), already scaled by its
        strength. */
    QVector3D glow;
    /*! Unit vectors towards the sun and the moon (x east, y up, z south). \qcshared */
    QVector3D sunDirection;
    QVector3D moonDirection;
    /*! 0 new moon, 0.5 full moon; one lunar month lasts QcSky::lunarDays days. */
    double moonPhase = 0.0;
    /*! 0 by day, 1 in the darkest night. */
    double stars = 0.0;
    /*! Drift of the cloud layer in metres (x, z; QcSky::withCloudDrift) and its brightness (lit by
        day, dark grey at night). \qcshared */
    double cloudOffsetX = 0.0;
    double cloudOffsetZ = 0.0;
    QVector3D cloudColor;
    /*! Sky light factor of the block shaders (QcDayCycle::daylight, less in rain, more in a
        flash). */
    double daylight = 1.0;
    /*! Light the shaders add to their floor, also where no sky light reaches: an explosion's flash
        for a moment (decision E-879), night vision (decision E-992) and the ambient light of a
        dimension (decision E-1003); 0 most of the time in the overworld. */
    double flashLight = 0.0;
    /*! Whether sun, moon, stars and clouds show and the sun casts shadows: the overworld's sky,
        not the fog of the nether or the vault of the end (decision E-1003). */
    bool celestial = true;
    /*! Whether sky light lights the world at all; without it not even the floor of the moonless
        night reaches the open sky (a dimension without sky light, decision E-1003). */
    bool skyLight = true;
    /*! How much of the sky the clouds cover beyond the fair-weather clouds: 0 clear, 1 overcast. */
    double overcast = 0.0;
    /*! Factor on the fog's distances: 1 clear, smaller in rain (the fog comes closer). */
    double fogScale = 1.0;
    /*! Where the fog starts and where it hides everything, as shares of the view distance before
        fogScale (the end moves half as much with it); a biome's atmosphere may set them
        (QcSky::withAtmosphere, decision E-1120). \qcshared */
    double fogStart = 0.6;
    double fogEnd = 1.0;

    /*! Returns the fog colour: the horizon, so terrain at the fog's end has the colour of the
        sky behind it. */
    QVector3D fogColor() const { return horizon; }
};

/*! \brief The fog of a biome's atmosphere as the client shows it (QcClientAtmosphere, decision
    E-1120): a colour and the fog's start and end as shares of the view distance, each with the
    share (0..1) by which it replaces the dimension's. \qcfields */
struct QcAtmosphereFog
{
    QVector3D colour;
    double colourShare = 0.0;
    double start = 0.6;
    double startShare = 0.0;
    double end = 1.0;
    double endShare = 0.0;
};

/*! \brief The sky as a function of the world time and the weather. */
namespace QcSky {

/*! Days of one lunar month. */
inline constexpr int lunarDays = 8;
/*! Height of the cloud layer above sea level (metres) and its drift speed (metres per second).
    \qcshared */
inline constexpr double cloudHeight = 160.0;
inline constexpr double cloudSpeed = 1.2;

/*! Returns the sky at \a worldTime (seconds, QcDayCycle), without weather and with the clouds
    where they start (no drift). */
QcSkyState at(double worldTime);
/*! The time of day a dimension without day cycle shows under an overworld sky: noon. */
inline constexpr double fixedNoon = QcDayCycle::dayLength * 0.25;

/*! Returns the sky of \a dimension at \a worldTime without weather (decision E-1003): the
    overworld's sky follows the time of day (at() at worldTime, or at fixedNoon without day cycle);
    the nether's is a close fog of the dimension's colour, the end's a dark vault, both without
    sun, moon, stars and clouds. Without sky light the daylight is 0; the ambient light (0..15)
    lifts the shaders' floor; a fog colour replaces the horizon. */
QcSkyState forDimension(const QcDimensionDefinition &dimension, double worldTime);
/*! Returns the linear colour (0..1 each) of \a rgb, 0xRRGGBB. */
QVector3D colourOf(quint32 rgb);
/*! Returns \a sky in the \a fog of a biome's atmosphere (decision E-1120): its colour replaces
    the horizon by its share (the whole vault of a sky without sun), its start and end replace
    the fog's distances by theirs, so that they hold whatever fogScale the dimension has. */
QcSkyState withAtmosphere(QcSkyState sky, const QcAtmosphereFog &fog);
/*! Returns \a sky with its clouds drifted for \a seconds of the world's animation clock
    (QcWorldMirror::animationSeconds, decision E-968): they drift whether the time of day runs or
    not and stand while the server pauses or is frozen. */
QcSkyState withCloudDrift(QcSkyState sky, double seconds);
/*! Returns \a sky seen from \a eyeY: below the world it fades to black, which it reaches at
    QcWorld::voidLimitY; fog, clouds, glow and stars fade with it (decision E-288). */
QcSkyState seenFrom(QcSkyState sky, double eyeY);
/*! Returns \a sky under the weather (decision E-395): \a rain (0..1) greys and darkens the dome,
    covers it with clouds, hides stars and the dusk glow, brings the fog closer and takes sky
    light away; \a thunder (0..1) darkens it further; a lightning \a flash (0..1) lights
    everything up for a moment. Only the shaders' factors change, so no section is meshed again. */
QcSkyState withWeather(QcSkyState sky, double rain, double thunder, double flash);
/*! Returns \a sky in an explosion's \a flash (0..1, QcExplosionEffects::flashAt; decision E-879):
    a warm light on the dome and the clouds and on everything around, in caves too. */
QcSkyState withBlast(QcSkyState sky, double flash);
/*! Returns \a sky as a player with the night vision effect sees it when he \a sees so (decision
    E-992): the blocks and bodies around him as bright as by day, the dome as it is. */
QcSkyState withNightVision(QcSkyState sky, bool sees);

} // namespace QcSky
