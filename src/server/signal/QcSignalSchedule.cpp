// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcSignalSchedule.h"

#include <algorithm>

void QcSignalSchedule::index(QcBlockPos pos, const Key &key)
{
    m_byPos.insert(pos, key);
    m_byChunk[pos.chunk()].insert(pos);
}

void QcSignalSchedule::unindex(QcBlockPos pos)
{
    m_byPos.remove(pos);
    const auto chunk = m_byChunk.find(pos.chunk());
    if (chunk != m_byChunk.end()) {
        chunk->remove(pos);
        if (chunk->isEmpty()) {
            m_byChunk.erase(chunk);
        }
    }
}

bool QcSignalSchedule::add(QcBlockPos pos, quint64 due, Priority priority)
{
    if (m_byPos.contains(pos)) {
        return false;
    }

    const Key key{due, static_cast<quint8>(priority), m_sequence++};
    m_entries.emplace(key, pos);
    index(pos, key);
    return true;
}

void QcSignalSchedule::relocate(QcBlockPos from, QcBlockPos to)
{
    const auto it = m_byPos.constFind(from);
    if (it == m_byPos.constEnd()) {
        return;
    }
    const Key key = *it;
    // One entry per position: the earlier of the two stays.
    if (const auto there = m_byPos.constFind(to); there != m_byPos.constEnd()) {
        if (*there < key) {
            remove(from);
            return;
        }
        remove(to);
    }
    unindex(from);
    m_entries[key] = to;
    index(to, key);
}

void QcSignalSchedule::remove(QcBlockPos pos)
{
    const auto it = m_byPos.constFind(pos);
    if (it == m_byPos.constEnd()) {
        return;
    }
    m_entries.erase(*it);
    unindex(pos);
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
    return std::pair{std::get<0>(first), static_cast<Priority>(std::get<1>(first))};
}

QList<QcBlockPos> QcSignalSchedule::takeGroup(quint64 due, Priority priority)
{
    QList<QcBlockPos> positions;
    auto it = m_entries.lower_bound(Key{due, static_cast<quint8>(priority), 0});
    while (it != m_entries.end() && std::get<0>(it->first) == due && std::get<1>(it->first) == static_cast<quint8>(priority)) {
        positions.append(it->second);
        unindex(it->second);
        it = m_entries.erase(it);
    }
    return positions;
}

QList<QcSignalSchedule::Entry> QcSignalSchedule::entries() const
{
    QList<Entry> list;
    list.reserve(static_cast<qsizetype>(m_entries.size()));
    for (const auto &[key, pos] : m_entries) {
        list.append({pos, std::get<0>(key), static_cast<Priority>(std::get<1>(key))});
    }
    return list;
}

QList<QcSignalSchedule::Entry> QcSignalSchedule::entriesIn(QcChunkPos chunk) const
{
    QList<std::pair<Key, QcBlockPos>> found;
    const QSet<QcBlockPos> positions = m_byChunk.value(chunk);
    for (const QcBlockPos &pos : positions) {
        found.append({m_byPos.value(pos), pos});
    }
    std::ranges::sort(found, [](const auto &a, const auto &b) { return a.first < b.first; });

    QList<Entry> list;
    for (const auto &[key, pos] : std::as_const(found)) {
        list.append({pos, std::get<0>(key), static_cast<Priority>(std::get<1>(key))});
    }
    return list;
}

void QcSignalSchedule::forgetChunk(QcChunkPos chunk)
{
    const QSet<QcBlockPos> positions = m_byChunk.take(chunk);
    for (const QcBlockPos &pos : positions) {
        m_entries.erase(m_byPos.take(pos));
    }
}

void QcSignalSchedule::clear()
{
    m_entries.clear();
    m_byPos.clear();
    m_byChunk.clear();
}
