// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <map>
#include <optional>
#include <tuple>
#include <utility>

// The scheduled ticks of circuit components (decision E-335): a component asks to be looked at again
// after a delay (a torch after 2 ticks, a repeater after its delay, a button when it springs back).
// One entry per position; asking again while one is pending keeps the earlier one. The entries of a
// chunk move and go with it (QcBlockEntities owns the schedule, decision E-357). Entries are due
// in the order of their tick and then their priority; everything of one tick and priority runs as
// one simultaneous step, so their order among each other never matters.
class QcSignalSchedule
{
public:
    // Steps within a tick, lowest first.
    enum class Priority : quint8 {
        // Buttons and plates springing back, lamps going dark, burnt-out torches lighting again.
        Release,
        // Torches and repeaters switching.
        Diode,
    };
    static constexpr int priorityCount = 2;

    struct Entry
    {
        QcBlockPos pos;
        quint64 due = 0;
        Priority priority = Priority::Diode;
    };

    // Adds an entry unless the position has one; false when it already had one.
    bool add(QcBlockPos pos, quint64 due, Priority priority);
    bool contains(QcBlockPos pos) const { return m_byPos.contains(pos); }
    void remove(QcBlockPos pos);
    // The entry of `from` (a component a piston moved) now belongs to `to`; when `to` has one too,
    // the earlier of the two stays.
    void relocate(QcBlockPos from, QcBlockPos to);
    // The tick and priority of the first group of entries due at `now` or before (overdue ones
    // first), if any.
    [[nodiscard]] std::optional<std::pair<quint64, Priority>> nextGroup(quint64 now) const;
    // Takes the entries of exactly that tick and priority.
    QList<QcBlockPos> takeGroup(quint64 due, Priority priority);
    qsizetype size() const { return m_byPos.size(); }
    QList<Entry> entries() const;
    // The entries of one chunk, in the order they are due; the chunks that have entries.
    QList<Entry> entriesIn(QcChunkPos chunk) const;
    QList<QcChunkPos> chunks() const { return m_byChunk.keys(); }
    bool hasEntriesIn(QcChunkPos chunk) const { return m_byChunk.contains(chunk); }
    // Drops the entries of a chunk (it was generated again).
    void forgetChunk(QcChunkPos chunk);
    void clear();

private:
    using Key = std::tuple<quint64, quint8, quint64>;

    void index(QcBlockPos pos, const Key &key);
    void unindex(QcBlockPos pos);

    std::map<Key, QcBlockPos> m_entries;
    QHash<QcBlockPos, Key> m_byPos;
    QHash<QcChunkPos, QSet<QcBlockPos>> m_byChunk;
    quint64 m_sequence = 0;
};
