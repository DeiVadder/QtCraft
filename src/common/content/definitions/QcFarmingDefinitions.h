// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcExperienceDrop.h"

#include <QList>
#include <QString>
#include <QStringList>

// A plant that ripens in stages (wheat, carrots, pumpkin stems), driven by random ticks on the
// server (decision E-289). Its stages are the values of the state property `property` in their
// order; the last value is ripe. A ripe plant with a `fruit` (stems) then puts that block onto a free
// neighbouring cell whose ground is one of `fruitGround`. Server side only.
struct QcCropDefinition
{
    // State property holding the stage; empty for blocks that are no crop.
    QString property;
    // Average seconds from the first to the ripe stage on wet soil in enough light.
    double averageSeconds = 1200.0;
    // Factor on the pace on soil that is not wet (and on any other ground).
    double dryFactor = 0.5;
    // Light (the brighter of sky and block light) a stage step needs, 0..15.
    int minLight = 9;
    // Block a ripe plant grows next to it, empty for none, and the average seconds for one fruit.
    QString fruit;
    double fruitSeconds = 300.0;
    QStringList fruitGround;
    // Chance per check (once a second) that a dropped item placing this crop, lying on free soil,
    // plants itself (decision E-294); 0 never.
    double plantChance = 0.0;

    bool isCrop() const { return !property.isEmpty(); }
    bool bearsFruit() const { return !fruit.isEmpty(); }
};

// Soil that crops grow on (farmland): random ticks keep it wet while `water` lies within `reach`
// blocks horizontally (same level or one above) and let it dry otherwise; dry soil without a crop on
// it, or soil under a full block, turns into `revertsTo`. Its moisture is the state property
// `property`: the last value is wet, the first one dry. Server side only.
struct QcSoilDefinition
{
    QString property;
    QString water;
    int reach = 4;
    QString revertsTo;

    bool isSoil() const { return !property.isEmpty(); }
};

// How a peaceful creature breeds (decision E-293): fed with one of `food`, an adult falls in love for
// `loveSeconds`; two of a kind in love within `partnerDistance` metres get a young one, which grows up
// after `growUpSeconds`. Parents may breed again after `cooldownSeconds`. Server side only.
struct QcBreedingDefinition
{
    QStringList food;
    double loveSeconds = 30.0;
    double cooldownSeconds = 300.0;
    double growUpSeconds = 1200.0;
    double partnerDistance = 8.0;
    // Experience points a pair gives as orbs when a young one is born (the reference's 1 to 7,
    // decision E-398). Server side only.
    QcExperienceDrop experience{1, 7};

    bool breeds() const { return !food.isEmpty(); }
};

// What a creature lays now and then (a chicken's eggs, decision E-452): an awake adult drops `item`
// at its feet after a time chosen evenly between `minSeconds` and `maxSeconds`, anew after each one,
// and `sound` plays there. Server side only.
struct QcLayingDefinition
{
    QString item;
    double minSeconds = 300.0;
    double maxSeconds = 600.0;
    QString sound;

    bool lays() const { return !item.isEmpty(); }
};

// A block that grows into a tree feature after a random time (saplings).
struct QcSaplingDefinition
{
    // Name of a tree or column feature; empty for blocks that are not saplings.
    QString feature;
    double minSeconds = 120.0;
    double maxSeconds = 300.0;
    // Blocks it may be planted on; empty allows any solid block.
    QList<QString> plantOn;
    // Chance that a despawning item of this block plants itself instead of vanishing.
    double plantChance = 0.0;
    // The giant tree feature that four of these saplings in a 2×2 square grow into together; empty
    // when they only grow alone (decision E-822). Server side only, not sent to clients.
    QString giantFeature;

    bool isSapling() const { return !feature.isEmpty(); }
};

// A plant that grows upwards by itself (sugar cane): random ticks of its top block add a block of the
// same kind above until the column is `maxHeight` tall. Server side only.
struct QcGrowthDefinition
{
    // Tallest column it grows to; 0 for blocks that do not grow.
    int maxHeight = 0;
    // Average seconds between two growth steps of one column.
    double averageSeconds = 600.0;

    bool grows() const { return maxHeight > 0; }
};
