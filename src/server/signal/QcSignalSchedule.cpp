#include "QcSignalSchedule.h"

bool QcSignalSchedule::add(QcBlockPos pos, quint64 due, Priority priority)
{
    if (m_byPos.contains(pos)) {
        return false;
    }

    const Key key{due, quint8(priority), m_sequence++};
    m_entries.emplace(key, pos);
    m_byPos.insert(pos, key);
    return true;
}

void QcSignalSchedule::relocate(QcBlockPos from, QcBlockPos to)
{
    const auto it = m_byPos.constFind(from);
    if (it == m_byPos.constEnd() || m_byPos.contains(to)) {
        return;
    }
    const Key key = *it;
    m_byPos.erase(it);
    m_entries[key] = to;
    m_byPos.insert(to, key);
}

void QcSignalSchedule::remove(QcBlockPos pos)
{
    const auto it = m_byPos.constFind(pos);
    if (it == m_byPos.constEnd()) {
        return;
    }
    m_entries.erase(*it);
    m_byPos.erase(it);
}

std::optional<std::pair<quint64, QcSignalSchedule::Priority>> QcSignalSchedule::nextGroup(quint64 now) const
{
    if (m_entries.empty()) {
        return std::nullopt;
    }

    const Key &first = m_entries.begin()->first;
    if (std::get<0>(first) > now) {
        return std::nullopt;
    }
    return std::pair{std::get<0>(first), Priority(std::get<1>(first))};
}

QList<QcBlockPos> QcSignalSchedule::takeGroup(quint64 due, Priority priority)
{
    QList<QcBlockPos> positions;
    auto it = m_entries.lower_bound(Key{due, quint8(priority), 0});
    while (it != m_entries.end() && std::get<0>(it->first) == due && std::get<1>(it->first) == quint8(priority)) {
        positions.append(it->second);
        m_byPos.remove(it->second);
        it = m_entries.erase(it);
    }
    return positions;
}

QList<QcSignalSchedule::Entry> QcSignalSchedule::entries() const
{
    QList<Entry> list;
    list.reserve(qsizetype(m_entries.size()));
    for (const auto &[key, pos] : m_entries) {
        list.append({pos, std::get<0>(key), Priority(std::get<1>(key))});
    }
    return list;
}

void QcSignalSchedule::clear()
{
    m_entries.clear();
    m_byPos.clear();
}
