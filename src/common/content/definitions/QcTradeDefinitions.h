#pragma once

#include <QList>
#include <QString>

// A number of items of one kind in a trade offer (by item name).
struct QcTradeStack
{
    QString item;
    int count = 1;

    bool operator==(const QcTradeStack &) const = default;
};

// One offer of a profession (decision E-414): from `level` on, a villager may offer `result` for
// `costs` (one or two stacks, paid by the player), `maxUses` times until it restocks at its
// workstation. Each trade gives the villager `villagerExperience` and the player
// `playerExperience` points as orbs. `demand` raises the price of the first cost by that many items
// for each restock after the offer sold out (falling again by one step for each restock it did not),
// at most to twice its base count.
struct QcTradeOffer
{
    static constexpr int maxCosts = 2;
    static constexpr int maxUsesLimit = 999;
    static constexpr int maxExperience = 1000;

    // Unique within the profession: the saved uses and demand of a villager belong to it.
    QString id;
    int level = 1;
    QList<QcTradeStack> costs;
    QcTradeStack result;
    int maxUses = 12;
    int villagerExperience = 2;
    int playerExperience = 1;
    int demand = 0;

    bool operator==(const QcTradeOffer &) const = default;
};

// A file of offers (trades/<name>.json): offers of one profession. A later pack's set of the same
// name replaces it, a set of another name adds its offers to the profession.
struct QcTradeSet
{
    static constexpr int maxOffers = 64;

    QString name;
    QString profession;
    QList<QcTradeOffer> offers;
};

// How villagers trade (villagers/<name>.json "trading", decision E-414): the experience a villager needs
// for each level (the first is 0, the fifth is master), how many offers of each level a villager
// draws from its profession's pool, and how often a day it restocks at its workstation, at least
// `restockGap` of a day apart.
struct QcTradingSettings
{
    static constexpr int levelCount = 5;

    QList<int> levels = {0, 10, 70, 150, 250};
    int offersPerLevel = 2;
    int restocksPerDay = 2;
    double restockGap = 0.1;
};
