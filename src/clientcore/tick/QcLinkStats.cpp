#include "clientcore/tick/QcLinkStats.h"

void QcLinkStats::inputSent(quint32 sequence, qint64 nowNs)
{
    const std::scoped_lock lock(m_mutex);
    m_sent[m_next] = {sequence, nowNs};
    m_next = (m_next + 1) % m_sent.size();
}

void QcLinkStats::acknowledged(quint32 sequence, qint64 nowNs)
{
    const std::scoped_lock lock(m_mutex);
    if (sequence == 0 || sequence == m_acknowledged) {
        return;
    }
    m_acknowledged = sequence;
    for (const Sent &sent : m_sent) {
        if (sent.sequence == sequence && sent.atNs > 0) {
            m_roundTrip.add(double(nowNs - sent.atNs) / 1e6);
            return;
        }
    }
}

void QcLinkStats::setTraffic(QcTraffic traffic)
{
    const std::scoped_lock lock(m_mutex);
    m_traffic = traffic;
}

QcLinkStats::Taken QcLinkStats::take()
{
    const std::scoped_lock lock(m_mutex);
    return {m_roundTrip.take(), m_traffic};
}
