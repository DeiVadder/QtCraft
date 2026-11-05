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

/*! \brief The scheduled ticks of circuit components (decision E-335): a component asks to be looked
    at again after a delay (a torch after 2 ticks, a repeater after its delay, a button when it
    springs back).

    One entry per position; asking again while one is pending keeps the earlier one. The entries
    of a chunk move and go with it (QcBlockEntities owns the schedule, decision E-357). Entries
    are due in the order of their tick and then their priority; everything of one tick and
    priority runs as one simultaneous step, so their order among each other never matters. */
class QcSignalSchedule
{
public:
    /*! \brief Steps within a tick, lowest first. */
    enum class Priority : quint8 {
        /*! Buttons and plates springing back, lamps going dark, burnt-out torches lighting
            again. */
        Release,
        /*! Torches and repeaters switching. */
        Diode,
    };
    /*! The number of priorities. */
    static constexpr int priorityCount = 2;

    /*! \brief A scheduled tick. */
    struct Entry
    {
        /*! The position of the component. */
        QcBlockPos pos;
        /*! The tick it is due. */
        quint64 due = 0;
        /*! Its step within the tick. */
        Priority priority = Priority::Diode;
    };

    /*! Adds an entry of \a pos due at \a due with \a priority unless the position has one.
        Returns \c false when it already had one. */
    bool add(QcBlockPos pos, quint64 due, Priority priority);
    /*! Returns whether \a pos has an entry. */
    bool contains(QcBlockPos pos) const { return m_byPos.contains(pos); }
    /*! Removes the entry of \a pos. */
    void remove(QcBlockPos pos);
    /*! The entry of \a from (a component a piston moved) now belongs to \a to; when \a to has
        one too, the earlier of the two stays. */
    void relocate(QcBlockPos from, QcBlockPos to);
    /*! Returns the tick and priority of the first group of entries due at \a now or before
        (overdue ones first), if any. */
    [[nodiscard]] std::optional<std::pair<quint64, Priority>> nextGroup(quint64 now) const;
    /*! Returns the positions of the entries of exactly the tick \a due and \a priority and takes
        them out. */
    QList<QcBlockPos> takeGroup(quint64 due, Priority priority);
    /*! Returns the number of entries. */
    qsizetype size() const { return m_byPos.size(); }
    /*! Returns every entry. */
    QList<Entry> entries() const;
    /*! Returns the entries of \a chunk, in the order they are due. */
    QList<Entry> entriesIn(QcChunkPos chunk) const;
    /*! Returns the chunks that have entries. */
    QList<QcChunkPos> chunks() const { return m_byChunk.keys(); }
    /*! Returns whether \a chunk has entries. */
    bool hasEntriesIn(QcChunkPos chunk) const { return m_byChunk.contains(chunk); }
    /*! Drops the entries of \a chunk (it was generated again). */
    void forgetChunk(QcChunkPos chunk);
    /*! Drops every entry. */
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
