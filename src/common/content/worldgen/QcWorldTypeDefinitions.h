// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

/*! \brief Rules a world type sets once when a world of it is created (decision E-223): a time of
    day that stands still (seconds after sunrise) or the running day, and whether creatures spawn
    by themselves. */
struct QcWorldTypeRules
{
    /*! Time of day in seconds after sunrise (\c {"timeOfDay"}); none: the world's time
        is left as it is. Setting it turns \c {daylightCycle} off unless the JSON sets that too. */
    std::optional<double> timeOfDay;
    /*! Whether the day runs (\c {"daylightCycle"}). */
    bool daylightCycle = true;
    /*! Whether creatures spawn by themselves (\c {"mobSpawning"}). */
    bool mobSpawning = true;
};

/*! \brief Settings of the flat world type (worldgen/flat.json): the layer preset a new flat
    world gets unless it names its own, "bedrock,59*stone,3*dirt,grass_block" (bottom up, \c {n*}
    repeats). */
struct QcFlatDefinition
{
    /*! The layer preset (\c {"preset"}, at most 4096 characters). */
    QString preset = QStringLiteral("qtcraft:bedrock,59*qtcraft:stone,3*qtcraft:dirt,qtcraft:grass_block");
    /*! Rules of a new flat world; by default the day runs and no creatures spawn. */
    QcWorldTypeRules rules{std::nullopt, true, false};
};

/*! \brief Settings of the void world type (worldgen/void.json): air everywhere except a square
    platform of \c {platform} blocks around the world's origin (radius 0: none) at height
    \c {platformY}. */
struct QcVoidDefinition
{
    /*! Block of the platform (\c {"platform"}). */
    QString platform = QStringLiteral("qtcraft:stone");
    /*! The platform covers x and z from -platformRadius to platformRadius
        (\c {"platformRadius"}, 0 to 16). */
    int platformRadius = 2;
    /*! Height of the platform (\c {"platformY"}). */
    int platformY = 63;
    /*! Rules of a new void world; by default noon standing still and no creatures spawning. */
    QcWorldTypeRules rules{150.0, false, false};
};
