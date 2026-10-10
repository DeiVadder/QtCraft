#include "QcServerDebugMirror.h"

#include <algorithm>

void QcServerDebugMirror::receive(const QcMsg::ServerDebug &message)
{
    m_latest = message;

    // recentTickMs ends with the duration of tick message.tick; only the ticks after the newest one
    // in the history are new.
    const qsizetype count = message.recentTickMs.size();
    qsizetype fresh = count;
    if (m_historyTick) {
        fresh = *m_historyTick >= message.tick ? 0 : qsizetype(std::min<quint64>(message.tick - *m_historyTick, quint64(count)));
    }
    if (fresh > 0) {
        m_tickHistory.append(message.recentTickMs.mid(count - fresh));
        m_historyTick = message.tick;
    }

    if (m_tickHistory.size() > historyTicks) {
        m_tickHistory.remove(0, m_tickHistory.size() - historyTicks);
    }
}

void QcServerDebugMirror::clear()
{
    m_latest.reset();
    m_tickHistory.clear();
    m_historyTick.reset();
}
