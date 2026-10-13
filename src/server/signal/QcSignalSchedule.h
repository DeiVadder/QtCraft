#pragma once

#include "QcBlockPos.h"

#include <QHash>
#include <QList>

#include <map>
#include <optional>
#include <tuple>
#include <utility>

// The scheduled ticks of circuit components (decision E-335): a component asks to be looked at again
// after a delay (a torch after 2 ticks, a repeater after its delay, a button when it springs back).
// One entry per position; asking again while one is pending keeps the earlier one. Entries are due
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
    // The entry of `from` (a dispenser a piston moved) now belongs to `to`, unless `to` has one.
    void relocate(QcBlockPos from, QcBlockPos to);
    // The tick and priority of the first group of entries due at `now` or before (overdue ones
    // first), if any.
    std::optional<std::pair<quint64, Priority>> nextGroup(quint64 now) const;
    // Takes the entries of exactly that tick and priority.
    QList<QcBlockPos> takeGroup(quint64 due, Priority priority);
    qsizetype size() const { return m_byPos.size(); }
    QList<Entry> entries() const;
    void clear();

private:
    using Key = std::tuple<quint64, quint8, quint64>;

    std::map<Key, QcBlockPos> m_entries;
    QHash<QcBlockPos, Key> m_byPos;
    quint64 m_sequence = 0;
};
