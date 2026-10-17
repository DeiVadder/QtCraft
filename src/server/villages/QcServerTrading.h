#pragma once

#include "common/protocol/QcMessages.h"
#include "server/villages/QcVillagerTrades.h"

#include <QSet>

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcCreature;
struct QcServerClient;

// Trading with villagers (decision E-414), server-authoritative. A player using a villager with a
// profession opens its trading screen (QcMsg::TradeState): one trader per villager at a time, who
// it stands facing while the screen is open. A trade pays the costs at their current price from
// the inventory and gives the result (what does not fit is thrown, like from the other screens),
// counts a use, gives the villager experience (a level reached adds offers) and drops the player's
// experience as orbs. The screen closes when the villager flees (danger, a hit), dies, goes to bed
// or sleeps, or the player dies, leaves, walks out of reach or opens a block's screen.
class QcServerTrading
{
public:
    // A repeated trade (shift-click) trades at most this often.
    static constexpr int maxRepeats = 64;

    QcServerTrading(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);

    // The player used the creature `id`: a villager with offers opens its trading screen, one without
    // turns to the player and speaks. False when it is no villager in reach (the use goes on).
    bool open(QcServerClient &client, quint32 id);
    void trade(QcServerClient &client, QcMsg::Trade message);
    // Ends the player's trade; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // Once per tick: closes screens whose villager or player is gone, fled or out of reach, keeps
    // the villagers facing their traders and sends what changed (a restock, a level).
    void update();

private:
    // The villager of the player's open screen while the trade may go on, else null.
    QcCreature *partnerOf(const QcServerClient &client);
    // Trades the offer at `index` once: pays, counts the use and gives the villager experience;
    // false when it cannot (sold out, the inventory lacks the costs, the villager's offer there is
    // another one).
    bool tradeOnce(QcServerClient &client, QcCreature &villager, const QcVillagerTrades::Offer &offer, qsizetype index);
    // Gives the player the results and experience of `traded` trades of the offer.
    void hand(QcServerClient &client, const QcCreature &villager, const QcVillagerTrades::Offer &offer, int traded);
    // The screen as the player sees it now.
    QcMsg::TradeState stateOf(const QcCreature &villager) const;
    void send(QcServerClient &client, const QcCreature &villager, bool force);
    bool inReach(const QcServerClient &client, const QcCreature &villager, double slack) const;
    const QcVillagerTrades &trades() const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
    // Villagers with an open trading screen; their traders are checked every tick.
    QSet<quint32> m_trading;
};
