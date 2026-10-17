#pragma once

#include <QtGlobal>

// The time of the server (decision E-249): simulated ticks, and the real time as the players live
// it. Each tick adds the real time it stands for (QcTickScheduler::tickRealNs); input budgets, rate
// limits, autosaves, confirmations and the pace of messages to the clients follow the players' time
// at every tick rate and during a sprint, the simulation follows the ticks.
class QcServerClock
{
public:
    quint64 tick() const { return m_tick; }
    qint64 playerTimeNs() const { return m_playerTimeNs; }
    // The real time the latest tick stood for.
    qint64 lastTickNs() const { return m_lastTickNs; }

    // A new tick that stands for `realNs` of the players' time.
    void advance(qint64 realNs)
    {
        ++m_tick;
        m_lastTickNs = realNs;
        m_playerTimeNs += realNs;
    }

private:
    quint64 m_tick = 0;
    qint64 m_playerTimeNs = 0;
    qint64 m_lastTickNs = 50'000'000;
};
