// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// What the weather does to the world (weather.json of a pack, decision E-396; server side only, a
// later pack replaces it). The course of the weather itself comes from the seed (decision E-395).
struct QcWeatherDefinition
{
    // Lightning ("lightning"): during a thunderstorm one strike near each player every `meanSeconds`
    // on average. A strike hurts players and creatures within `radius` blocks (horizontally; from
    // `radius` below to six blocks above the struck cell) by `damage` (players through their
    // armour) and sets them alight for `burnSeconds`; it lights at most `sparks` fires at and right
    // around the struck cell, each a spark of its own within the guarantees of fire (decision E-392).
    struct Lightning
    {
        double meanSeconds = 10.0;
        double damage = 5.0;
        int radius = 3;
        int burnSeconds = 8;
        int sparks = 2;
    };

    // Snow ("snow"): while it snows, `columnsPerTick` random columns near each player per tick get a
    // `layer` block on their top where they may; no layer when the block is missing or the count 0.
    struct Snow
    {
        QString layer = QStringLiteral("qtcraft:snow_layer");
        int columnsPerTick = 4;
    };

    Lightning lightning;
    Snow snow;
};
