#pragma once

#include "common/protocol/QcMessages.h"

#include <QObject>

#include <functional>

// The trading screen of a villager the server opened for the local player (decision E-414): the
// villager, its profession, level and experience and its offers (QcMsg::TradeState), and the trades
// the player asks for. Part of QcClientSession; GUI-free.
class QcClientTrading : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;

    void setSender(Sender sender) { m_send = std::move(sender); }
    // Takes over TradeState and CloseTrade; false for other messages. The offers keep the server's
    // order (a trade names its index); the content of the session knows their items.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_state.villager != 0; }
    const QcMsg::TradeState &state() const { return m_state; }
    // Asks the server to trade the offer at `index` once, or as often as possible (`repeat`).
    // False when no screen is open or there is no such offer.
    bool trade(int index, bool repeat);
    // Whether the `inventory` slots hold the costs of the offer at `index` (two costs of one item
    // add up).
    bool affordable(int index, const QList<QcItemStack> &inventory) const;
    // The player closed the screen. A TradeState still on its way is an answer to a use the player
    // already closed again (the server closed it too): such states are ignored until the player
    // uses a creature again (usedEntity()).
    void close();
    void usedEntity() { m_closedLocally = false; }

signals:
    void opened();
    void changed();
    void closed();

private:
    Sender m_send;
    QcMsg::TradeState m_state;
    bool m_closedLocally = false;
};
