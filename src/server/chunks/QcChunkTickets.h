// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

#include <QHash>
#include <QList>

#include <functional>

// Load tickets of circuits (decision E-376): chunks loaded for a while although no player is near.
// Circuits ask for one when a signal reaches a part of its line that is not loaded
// (QcCircuitState::waitingOn), so a long line switches a far door. Tickets are bounded per tick and
// in the world (QcServerLimits) and last a fixed number of ticks. A ticket weighs as many chunks as
// the group it loads (decision E-375), so `total` bounds the chunks tickets keep loaded; a chunk asked for again while it
// has a ticket keeps its first one, so a clock cannot keep a chunk loaded forever by itself.
class QcChunkTickets
{
public:
    struct Limits
    {
        qsizetype perTick = 4;
        qsizetype total = 256;
        quint64 ticks = 100;
    };

    void setLimits(const Limits &limits) { m_limits = limits; }
    const Limits &limits() const { return m_limits; }

    // Gives `chunk` a ticket for a group of `weight` chunks; false when the limits refuse it. A chunk
    // with a ticket keeps it. A refusal counts unless `retry` (a wish refused before, asked again).
    bool request(QcChunkPos chunk, quint64 now, qsizetype weight = 1, bool retry = false);
    // Tickets can be given at all (limits above zero).
    bool canGive() const { return m_limits.perTick > 0 && m_limits.total > 0; }
    // What a signal does in a chunk with a ticket: nothing, passes through (changes or scheduled
    // ticks) or waits there for the next chunk of its way.
    enum class Use { Free, Busy, Waiting };
    // Ends the tickets that ran out; starts the budget of a new tick. A ticket's time counts from the
    // tick its chunk is loaded (reading it may take a while), so it lasts `ticks` loaded ticks. A
    // signal is never taken into storage halfway (decision E-378): while one waits in the chunk the
    // ticket stays, while one passes through it lasts up to `ticks` more after that.
    void expire(quint64 now, const std::function<bool(QcChunkPos)> &isLoaded, const std::function<Use(QcChunkPos)> &use = {});
    bool contains(QcChunkPos chunk) const { return m_tickets.contains(chunk); }
    QList<QcChunkPos> chunks() const { return m_tickets.keys(); }
    qsizetype count() const { return m_tickets.size(); }
    // Chunks the tickets load, their groups counted whole.
    qsizetype weight() const { return m_weight; }
    // Tickets refused by the limits since the world opened.
    quint64 refused() const { return m_refused; }
    // Tickets given since the world opened.
    quint64 given() const { return m_given; }
    // Counts every ticket given or ended (QcChunkLoading recomputes the needed chunks on a change).
    quint64 changes() const { return m_changes; }
    void clear()
    {
        m_tickets.clear();
        m_weight = 0;
        ++m_changes;
    }

private:
    Limits m_limits;
    struct Ticket
    {
        // The tick it ends at.
        quint64 end = 0;
        qsizetype weight = 1;
    };
    QHash<QcChunkPos, Ticket> m_tickets;
    qsizetype m_weight = 0;
    qsizetype m_givenThisTick = 0;
    quint64 m_refused = 0;
    quint64 m_given = 0;
    quint64 m_changes = 0;
};
