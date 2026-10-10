#pragma once

#include "QcDebugMessages.h"

#include <QList>

#include <optional>

// What the client knows of the server's debug data (QcMsg::ServerDebug, decision E-225): the
// latest message and the durations of the latest ticks for the tick graph, each tick once (a
// message reports the ticks up to its own; those the graph already has are skipped).
class QcServerDebugMirror
{
public:
    // Ticks kept for the graph: twelve seconds.
    static constexpr qsizetype historyTicks = 240;

    void receive(const QcMsg::ServerDebug &message);
    void clear();

    const std::optional<QcMsg::ServerDebug> &latest() const { return m_latest; }
    // Tick durations in milliseconds, oldest first.
    const QList<float> &tickHistory() const { return m_tickHistory; }

private:
    std::optional<QcMsg::ServerDebug> m_latest;
    QList<float> m_tickHistory;
    // The server tick of the newest duration in m_tickHistory.
    std::optional<quint64> m_historyTick;
};
