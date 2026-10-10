#pragma once

#include "QcDebugMessages.h"

#include <QtGlobal>

class QcBlockUpdateQueue;
class QcTickPhaseTimer;
class QcTickScheduler;
struct QcServerClient;
struct QcServerState;

// Debug data for the clients that ask for it (QcMsg::DebugSubscribe, decision E-225): the tick
// phases of the server's window, its counts and, on request, where the creatures near the player
// are heading. Sent at most twice a second per client and only while subscribed, so a client
// without a debug display costs nothing. Only the local player gets it: the counts of the whole
// server and the positions of chased players are not for every client (until operators exist).
class QcServerDebug
{
public:
    static constexpr quint64 intervalTicks = 10;
    // Creatures this close to the player report their goal.
    static constexpr double goalRange = 48.0;

    QcServerDebug(const QcServerState &state, const QcBlockUpdateQueue &updates, const QcTickPhaseTimer &phases,
                  const QcTickScheduler &ticks);

    // `flags` as in QcMsg::DebugSubscribe, the latest subscription counts. The first message
    // follows with the next tick, unless the previous one went out less than intervalTicks ago (a
    // flood of subscriptions does not speed up the messages). Remote clients get nothing.
    void subscribe(QcServerClient &client, quint8 flags) const;
    // Sends the client its debug data when it is subscribed and the interval has passed.
    void update(QcServerClient &client) const;
    QcMsg::ServerDebug build(const QcServerClient &client) const;

private:
    void addGoals(const QcServerClient &client, QcMsg::ServerDebug &message) const;

    const QcServerState &m_state;
    const QcBlockUpdateQueue &m_updates;
    const QcTickPhaseTimer &m_phases;
    const QcTickScheduler &m_ticks;
};
