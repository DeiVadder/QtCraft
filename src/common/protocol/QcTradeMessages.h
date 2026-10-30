// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of trading with villagers (decision E-414).
// Part of the QcMessage variant (QcMessages.h).

#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

namespace QcMsg {

/*! \brief One offer as the trading screen shows it: what the player pays (one or two stacks, the
    first at its current price), what they get, and how often it can still be traded before the
    villager restocks. */
struct TradeOffer
{
    /*! The most stacks the player pays; the decoder refuses more. */
    static constexpr int maxCosts = 2;

    /*! What the player pays (see the struct). */
    QList<QcItemStack> costs;
    /*! What the player gets. */
    QcItemStack result;
    /*! How often it was traded since the last restock, and how often it can be traded between
        two restocks. \qcshared */
    qint32 uses = 0;
    qint32 maxUses = 0;

    bool operator==(const TradeOffer &) const = default;
};

/*! \brief Server -> client: the trading screen of the villager \c {villager} opened or changed (a
    trade, a restock, a level gained).

    \c {profession} is the translation key of its profession; \c {experience} runs from
    \c {levelStart} to \c {levelEnd} (equal at the highest level). */
struct TradeState
{
    /*! The most offers; the decoder refuses more. */
    static constexpr int maxOffers = 32;

    /*! The villager's entity id. */
    quint32 villager = 0;
    /*! The translation key of its profession. */
    QString profession;
    /*! Its level. */
    qint32 level = 1;
    /*! Its experience, and where its level starts and ends (see the struct). \qcshared */
    qint32 experience = 0;
    qint32 levelStart = 0;
    qint32 levelEnd = 0;
    /*! Its offers. */
    QList<TradeOffer> offers;

    bool operator==(const TradeState &) const = default;
};

/*! \brief Client -> server: trade the offer at index \c {offer} of the open screen once, or with
    \c {repeat} as often as the inventory and the uses left allow (shift-click). */
struct Trade
{
    /*! The villager's entity id. */
    quint32 villager = 0;
    /*! The index of the offer. */
    qint32 offer = 0;
    /*! Trades as often as possible (see the struct). */
    bool repeat = false;
};

/*! \brief Client -> server: the player closed the trading screen.

    Server -> client: the screen closed (the villager fled, died or the player walked away). */
struct CloseTrade
{
};

} // namespace QcMsg
