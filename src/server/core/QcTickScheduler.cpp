#include "QcTickScheduler.h"

#include <QLoggingCategory>

#include <algorithm>
#include <chrono>

namespace {

Q_LOGGING_CATEGORY(lcTicks, "qtcraft.server")

} // namespace

QcTickScheduler::QcTickScheduler(Clock clock)
    : m_clock(std::move(clock))
{
}

QcTickScheduler::Clock QcTickScheduler::steadyClock()
{
    return [] {
        return qint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    };
}

void QcTickScheduler::start()
{
    m_nextDueNs = m_clock() + intervalNs;
}

qint64 QcTickScheduler::runDue(const std::function<void()> &tick)
{
    const int count = takeDueTicks(m_clock());

    for (int i = 0; i < count; ++i) {
        const qint64 started = m_clock();
        m_currentLateNs = started - m_nextDueNs;
        tick();
        m_stats.record(m_currentLateNs, m_clock() - started);
        m_nextDueNs += intervalNs;
        m_currentLateNs = 0;
    }

    return std::max<qint64>(0, m_nextDueNs - m_clock());
}

int QcTickScheduler::takeDueTicks(qint64 now)
{
    if (now < m_nextDueNs) {
        return 0;
    }

    const qint64 due = (now - m_nextDueNs) / intervalNs + 1;
    if (due <= maxCatchUpTicks) {
        return int(due);
    }

    // The dropped ticks are lost game time; the grid moves on so no drift builds up.
    const qint64 skipped = due - maxCatchUpTicks;
    m_nextDueNs += skipped * intervalNs;
    m_stats.addSkipped(quint64(skipped));
    qCWarning(lcTicks) << "Server overloaded, skipped" << skipped << "ticks";
    return maxCatchUpTicks;
}
