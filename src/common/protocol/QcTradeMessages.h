#pragma once

// Protocol messages of trading with villagers (decision E-414).
// Part of the QcMessage variant (QcMessages.h).

#include "QcItemStack.h"

#include <QList>
#include <QString>

namespace QcMsg {

// One offer as the trading screen shows it: what the player pays (one or two stacks, the first at
// its current price), what they get, and how often it can still be traded before the villager
// restocks.
struct TradeOffer
{
    static constexpr int maxCosts = 2;

    QList<QcItemStack> costs;
    QcItemStack result;
    qint32 uses = 0;
    qint32 maxUses = 0;

    bool operator==(const TradeOffer &) const = default;
};

// Server -> client: the trading screen of the villager `villager` opened or changed (a trade, a
// restock, a level gained). `profession` is the translation key of its profession; `experience`
// runs from `levelStart` to `levelEnd` (equal at the highest level).
struct TradeState
{
    static constexpr int maxOffers = 32;

    quint32 villager = 0;
    QString profession;
    qint32 level = 1;
    qint32 experience = 0;
    qint32 levelStart = 0;
    qint32 levelEnd = 0;
    QList<TradeOffer> offers;

    bool operator==(const TradeState &) const = default;
};

// Client -> server: trade the offer at index `offer` of the open screen once, or with `repeat` as
// often as the inventory and the uses left allow (shift-click).
struct Trade
{
    quint32 villager = 0;
    qint32 offer = 0;
    bool repeat = false;
};

// Client -> server: the player closed the trading screen. Server -> client: the screen closed (the
// villager fled, died or the player walked away).
struct CloseTrade
{
};

} // namespace QcMsg
