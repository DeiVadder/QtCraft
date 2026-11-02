// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QObject>

#include <functional>

/*! \brief The trading screen of a villager the server opened for the local player (decision E-414):
    the villager, its profession, level and experience and its offers (QcMsg::TradeState), and the
    trades the player asks for.

    Part of QcClientSession; GUI-free. */
class QcClientTrading : public QObject
{
    Q_OBJECT

public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;

    /*! Sets the way out to the server to \a sender (the session's connection). */
    void setSender(Sender sender) { m_send = std::move(sender); }
    /*! Takes over \a message when it is TradeState or CloseTrade. Returns false for other
        messages. The offers keep the server's order (a trade names its index); the content of
        the session knows their items. */
    bool handle(const QcMessage &message);

    /*! Returns whether a trading screen is open. */
    bool isOpen() const { return m_state.villager != 0; }
    /*! Returns the villager and its offers as the server last sent them. */
    const QcMsg::TradeState &state() const { return m_state; }
    /*! Asks the server to trade the offer at \a index once, or as often as possible (\a repeat).
        Returns false when no screen is open or there is no such offer. */
    bool trade(int index, bool repeat);
    /*! Returns whether the \a inventory slots hold the costs of the offer at \a index (two costs
        of one item add up). */
    bool affordable(int index, const QList<QcItemStack> &inventory) const;
    /*! The player closed the screen. A TradeState still on its way is an answer to a use the
        player already closed again (the server closed it too): such states are ignored until the
        player uses a creature again (usedEntity()). */
    void close();
    /*! The player used a creature: TradeState messages count again (see close()). */
    void usedEntity() { m_closedLocally = false; }

signals:
    /*! The server opened the trading screen of a villager. */
    void opened();
    /*! A new TradeState of the open villager arrived (offers, level or experience). */
    void changed();
    /*! The screen closed (by the server or by close()). */
    void closed();

private:
    Sender m_send;
    QcMsg::TradeState m_state;
    bool m_closedLocally = false;
};
