#pragma once

#include <QVector3D>

// Look of the sky at a moment of the day, as a pure function of the world time: colours of the
// dome, where sun and moon stand, the moon's phase, how visible the stars are and how far the
// clouds have drifted. The renderer draws the dome from it; fog takes the horizon colour so the
// far terrain fades into the sky.
struct QcSkyState
{
    // Linear RGB in 0..1.
    QVector3D zenith;
    QVector3D horizon;
    // Warm glow around the sun near the horizon (sunrise, sunset), already scaled by its strength.
    QVector3D glow;
    // Unit vectors towards the sun and the moon (x east, y up, z south).
    QVector3D sunDirection;
    QVector3D moonDirection;
    // 0 new moon, 0.5 full moon; one lunar month lasts QcSky::lunarDays days.
    double moonPhase = 0.0;
    // 0 by day, 1 in the darkest night.
    double stars = 0.0;
    // Drift of the cloud layer in metres (x, z) and its brightness (lit by day, dark grey at night).
    double cloudOffsetX = 0.0;
    double cloudOffsetZ = 0.0;
    QVector3D cloudColor;
    // Sky light factor of the block shaders (QcDayCycle::daylight).
    double daylight = 1.0;

    // Fog colour: the horizon, so terrain at the fog's end has the colour of the sky behind it.
    QVector3D fogColor() const { return horizon; }
};

namespace QcSky {

inline constexpr int lunarDays = 8;
// Height of the cloud layer above sea level (metres) and its drift speed (metres per second).
inline constexpr double cloudHeight = 160.0;
inline constexpr double cloudSpeed = 1.2;

QcSkyState at(double worldTime);

} // namespace QcSky
