#include "server/chunks/QcChunkTickets.h"

#include <algorithm>

bool QcChunkTickets::request(QcChunkPos chunk, quint64 now, qsizetype weight, bool retry)
{
    if (m_tickets.contains(chunk)) {
        return true;
    }
    weight = std::max<qsizetype>(1, weight);
    if (m_givenThisTick >= m_limits.perTick || m_weight + weight > m_limits.total) {
        m_refused += retry ? 0 : 1;
        return false;
    }
    ++m_givenThisTick;
    ++m_given;
    ++m_changes;
    m_weight += weight;
    m_tickets.insert(chunk, {now + m_limits.ticks, weight});
    return true;
}

void QcChunkTickets::expire(quint64 now, const std::function<bool(QcChunkPos)> &isLoaded, const std::function<Use(QcChunkPos)> &use)
{
    m_givenThisTick = 0;
    for (auto it = m_tickets.begin(); it != m_tickets.end();) {
        if (!isLoaded(it.key())) {
            it->end = now + m_limits.ticks;
            ++it;
            continue;
        }
        if (it->end > now) {
            ++it;
            continue;
        }

        const Use used = use ? use(it.key()) : Use::Free;
        if (used == Use::Waiting) {
            it->end = now;
        }
        if (used == Use::Waiting || (used == Use::Busy && now < it->end + m_limits.ticks)) {
            ++it;
            continue;
        }
        m_weight -= it->weight;
        ++m_changes;
        it = m_tickets.erase(it);
    }
}
