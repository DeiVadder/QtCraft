// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief What the weather does to the world (weather.json of a pack, decision E-396; server side
    only, a later pack replaces it).

    The course of the weather itself comes from the seed (decision E-395). */
struct QcWeatherDefinition
{
    /*! \brief Lightning ("lightning"): during a thunderstorm one strike near each player every
        \c {meanSeconds} on average.

        A strike hurts players and creatures within \c {radius} blocks (horizontally; from
        \c {radius} below to six blocks above the struck cell) by \c {damage} (players through
        their armour) and sets them alight for \c {burnSeconds}; it lights at most \c {sparks}
        fires at and right around the struck cell, each a spark of its own within the guarantees
        of fire (decision E-392). The JSON keys are the field names. \qcfields */
    struct Lightning
    {
        double meanSeconds = 10.0;
        double damage = 5.0;
        int radius = 3;
        int burnSeconds = 8;
        int sparks = 2;
    };

    /*! \brief Snow ("snow"): while it snows, \c {columnsPerTick} random columns near each player
        per tick get a \c {layer} block on their top where they may; no layer when the block is
        missing or the count 0. The JSON keys are the field names. \qcfields */
    struct Snow
    {
        QString layer = QStringLiteral("qtcraft:snow_layer");
        int columnsPerTick = 4;
    };

    /*! Lightning during thunderstorms (\c {"lightning"}). */
    Lightning lightning;
    /*! Snow layers while it snows (\c {"snow"}). */
    Snow snow;
};
