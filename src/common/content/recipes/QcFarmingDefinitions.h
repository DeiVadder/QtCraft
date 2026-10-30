// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcExperienceDrop.h"

#include <QList>
#include <QString>
#include <QStringList>

/*! \brief A plant that ripens in stages (wheat, carrots, pumpkin stems), driven by random ticks on
    the server (decision E-289).

    Its stages are the values of the state property \c {property} in their order; the last value
    is ripe. A ripe plant with a \c {fruit} (stems) then puts that block onto a free neighbouring
    cell whose ground is one of \c {fruitGround}. Server side only. */
struct QcCropDefinition
{
    /*! State property holding the stage; empty for blocks that are no crop. */
    QString property;
    /*! Average seconds from the first to the ripe stage on wet soil in enough light. */
    double averageSeconds = 1200.0;
    /*! Factor on the pace on soil that is not wet (and on any other ground). */
    double dryFactor = 0.5;
    /*! Light (the brighter of sky and block light) a stage step needs, 0..15. */
    int minLight = 9;
    /*! Block a ripe plant grows next to it (\c {"fruit"}); empty for none. */
    QString fruit;
    /*! Average seconds for one fruit (\c {"fruitSeconds"}, 1 to 86400). */
    double fruitSeconds = 300.0;
    /*! Blocks the cell below a fruit may be (\c {"fruitGround"}); required with a fruit. */
    QStringList fruitGround;
    /*! Chance per check (once a second) that a dropped item placing this crop, lying on free
        soil, plants itself (decision E-294); 0 never. */
    double plantChance = 0.0;

    /*! Returns whether the block is a crop (\c {property} is set). */
    bool isCrop() const { return !property.isEmpty(); }
    /*! Returns whether a ripe plant grows a fruit (\c {fruit} is set). */
    bool bearsFruit() const { return !fruit.isEmpty(); }
};

/*! \brief Soil that crops grow on (farmland): random ticks keep it wet while \c {water} lies within
    \c {reach} blocks horizontally (same level or one above) and let it dry otherwise; dry soil
    without a crop on it, or soil under a full block, turns into \c {revertsTo}.

    Its moisture is the state property \c {property}: the last value is wet, the first one dry.
    Server side only. */
struct QcSoilDefinition
{
    /*! State property holding the moisture (\c {"property"}, default \c {"moist"}); empty for
        blocks that are no soil. */
    QString property;
    /*! Block that keeps it wet (\c {"water"}, default \c {"qtcraft:water"}). */
    QString water;
    /*! Horizontal distance in blocks at which water keeps it wet (\c {"reach"}, 0 to 8). */
    int reach = 4;
    /*! Block it turns into (\c {"revertsTo"}, required, another block). */
    QString revertsTo;

    /*! Returns whether the block is soil (\c {property} is set). */
    bool isSoil() const { return !property.isEmpty(); }
};

/*! \brief How a peaceful creature breeds (decision E-293): fed with one of \c {food}, an adult
    falls in love for \c {loveSeconds}; two of a kind in love within \c {partnerDistance} metres get
    a young one, which grows up after \c {growUpSeconds}.

    Parents may breed again after \c {cooldownSeconds}. Server side only. */
struct QcBreedingDefinition
{
    /*! Items that make an adult fall in love (\c {"food"}, required); empty for creatures that do
        not breed. */
    QStringList food;
    /*! Seconds an adult stays in love (\c {"loveSeconds"}, 1 to 3600). */
    double loveSeconds = 30.0;
    /*! Seconds before parents may breed again (\c {"cooldownSeconds"}, 0 to 86400). */
    double cooldownSeconds = 300.0;
    /*! Seconds until a young one grows up (\c {"growUpSeconds"}, 1 to 86400). */
    double growUpSeconds = 1200.0;
    /*! Greatest distance between two partners, in metres (\c {"partnerDistance"}, 0.5 to 32). */
    double partnerDistance = 8.0;
    /*! Experience points a pair gives as orbs when a young one is born (the reference's 1 to 7,
        decision E-398). Server side only. */
    QcExperienceDrop experience{1, 7};

    /*! Returns whether the creature breeds (\c {food} is set). */
    bool breeds() const { return !food.isEmpty(); }
};

/*! \brief What a creature lays now and then (a chicken's eggs, decision E-452): an awake adult
    drops \c {item} at its feet after a time chosen evenly between \c {minSeconds} and
    \c {maxSeconds}, anew after each one, and \c {sound} plays there.

    Server side only. */
struct QcLayingDefinition
{
    /*! Qualified item it lays (\c {"item"}, required); empty for creatures that lay nothing. */
    QString item;
    /*! Shortest time between two items, in seconds (\c {"minSeconds"}, 1 to 86400). */
    double minSeconds = 300.0;
    /*! Longest time between two items, in seconds (\c {"maxSeconds"}, at least
        \c {minSeconds}). */
    double maxSeconds = 600.0;
    /*! Sound key played where it lays (\c {"sound"}); empty for none. */
    QString sound;

    /*! Returns whether the creature lays anything (\c {item} is set). */
    bool lays() const { return !item.isEmpty(); }
};

/*! \brief A block that grows into a tree feature after a random time (saplings). */
struct QcSaplingDefinition
{
    /*! Name of a tree or column feature; empty for blocks that are not saplings. */
    QString feature;
    /*! Shortest time until it grows, in seconds (\c {"minSeconds"}, 1 to 86400). */
    double minSeconds = 120.0;
    /*! Longest time until it grows, in seconds (\c {"maxSeconds"}, \c {minSeconds} to
        86400). */
    double maxSeconds = 300.0;
    /*! Blocks it may be planted on; empty allows any solid block. */
    QList<QString> plantOn;
    /*! Chance that a despawning item of this block plants itself instead of vanishing. */
    double plantChance = 0.0;
    /*! The giant tree feature that four of these saplings in a 2×2 square grow into together;
        empty when they only grow alone (decision E-822). Server side only, not sent to clients. */
    QString giantFeature;

    /*! Returns whether the block is a sapling (\c {feature} is set). */
    bool isSapling() const { return !feature.isEmpty(); }
};

/*! \brief A plant that grows upwards by itself (sugar cane): random ticks of its top block add a
    block of the same kind above until the column is \c {maxHeight} tall.

    Server side only. */
struct QcGrowthDefinition
{
    /*! Tallest column it grows to; 0 for blocks that do not grow. */
    int maxHeight = 0;
    /*! Average seconds between two growth steps of one column. */
    double averageSeconds = 600.0;

    /*! Returns whether the block grows upwards (\c {maxHeight} above 0). */
    bool grows() const { return maxHeight > 0; }
};
