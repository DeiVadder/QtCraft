// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief A number of items of one kind in a trade offer (by item name).

    Read from \c {{"item": name, "count": n}}: \c {item} the qualified item name, \c {count}
    1..64 (default 1). \qcfields */
struct QcTradeStack
{
    QString item;
    int count = 1;

    bool operator==(const QcTradeStack &) const = default;
};

/*! \brief One offer of a profession (decision E-414): from \c {level} on, a villager may offer
    \c {result} for \c {costs} (one or two stacks, paid by the player), \c {maxUses} times until it
    restocks at its workstation.

    Each trade gives the villager \c {villagerExperience} and the player \c {playerExperience}
    points as orbs. \c {demand} raises the price of the first cost by that many items for each
    restock after the offer sold out (falling again by one step for each restock it did not), at
    most to twice its base count. */
struct QcTradeOffer
{
    /*! Most cost stacks of an offer. */
    static constexpr int maxCosts = 2;
    /*! Highest \c {maxUses} the data may set. */
    static constexpr int maxUsesLimit = 999;
    /*! Highest villager or player experience of one trade the data may set. */
    static constexpr int maxExperience = 1000;

    /*! Unique within the profession: the saved uses and demand of a villager belong to it
        (\c {"id"}; default: first cost and result, as in "wheat>emerald"). */
    QString id;
    /*! Villager level from which it may be offered (\c {"level"}, 1..5). */
    int level = 1;
    /*! What the player pays (\c {"costs"}, 1 to maxCosts stacks). */
    QList<QcTradeStack> costs;
    /*! What the player gets (\c {"result"}). */
    QcTradeStack result;
    /*! Trades until it sells out and waits for a restock (\c {"maxUses"}). */
    int maxUses = 12;
    /*! Experience the villager gains per trade (\c {"villagerExperience"}). */
    int villagerExperience = 2;
    /*! Experience points the player gets as orbs per trade (\c {"playerExperience"}). */
    int playerExperience = 1;
    /*! Items added to the first cost per restock after the offer sold out (\c {"demand"},
        0..64). */
    int demand = 0;

    bool operator==(const QcTradeOffer &) const = default;
};

/*! \brief A file of offers (trades/<name>.json): offers of one profession.

    A later pack's set of the same name replaces it, a set of another name adds its offers to the
    profession. */
struct QcTradeSet
{
    /*! Most offers of a set. */
    static constexpr int maxOffers = 64;

    /*! Name of the set (\c {"name"}). */
    QString name;
    /*! The profession whose pool the offers join (\c {"profession"}). */
    QString profession;
    /*! The offers (\c {"offers"}), 1 to maxOffers with distinct ids. */
    QList<QcTradeOffer> offers;
};

/*! \brief How villagers trade (villagers/<name>.json "trading", decision E-414): the experience
    a villager needs for each level (the first is 0, the fifth is master), how many offers of
    each level a villager draws from its profession's pool, and how often a day it restocks at
    its workstation, at least \c {restockGap} of a day apart. */
struct QcTradingSettings
{
    /*! Number of villager levels. */
    static constexpr int levelCount = 5;

    /*! Experience needed for each level (\c {"levels"}): levelCount values, starting at 0 and
        rising. */
    QList<int> levels = {0, 10, 70, 150, 250};
    /*! Offers of each level a villager draws (\c {"offersPerLevel"}, 1..8). */
    int offersPerLevel = 2;
    /*! Restocks per day (\c {"restocksPerDay"}, 0..8). */
    int restocksPerDay = 2;
    /*! Least time between two restocks as a share of a day (\c {"restockGap"}, 0..1). */
    double restockGap = 0.1;
};
