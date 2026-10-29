// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

// Rules a world type sets once when a world of it is created (decision E-223): a time of day that
// stands still (seconds after sunrise) or the running day, and whether creatures spawn by themselves.
struct QcWorldTypeRules
{
    std::optional<double> timeOfDay;
    bool daylightCycle = true;
    bool mobSpawning = true;
};

// Settings of the flat world type (worldgen/flat.json): the layer preset a new flat world gets
// unless it names its own, "bedrock,59*stone,3*dirt,grass_block" (bottom up, `n*` repeats).
struct QcFlatDefinition
{
    QString preset = QStringLiteral("qtcraft:bedrock,59*qtcraft:stone,3*qtcraft:dirt,qtcraft:grass_block");
    QcWorldTypeRules rules{std::nullopt, true, false};
};

// Settings of the void world type (worldgen/void.json): air everywhere except a square platform of
// `platform` blocks around the spawn (radius 0: none) at height `platformY`.
struct QcVoidDefinition
{
    QString platform = QStringLiteral("qtcraft:stone");
    int platformRadius = 2;
    int platformY = 63;
    QcWorldTypeRules rules{150.0, false, false};
};
