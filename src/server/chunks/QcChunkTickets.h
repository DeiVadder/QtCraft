// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

#include <QHash>
#include <QList>

#include <functional>

/*! \brief Load tickets of circuits (decision E-376): chunks loaded for a while although no player
    is near.

    Circuits ask for one when a signal reaches a part of its line that is not loaded
    (QcCircuitState::waitingOn), so a long line switches a far door. Tickets are bounded per tick
    and in the world (QcServerLimits) and last a fixed number of ticks. A ticket weighs as many
    chunks as the group it loads (decision E-375), so \c {total} bounds the chunks tickets keep
    loaded; a chunk asked for again while it has a ticket keeps its first one, so a clock cannot
    keep a chunk loaded forever by itself. */
class QcChunkTickets
{
public:
    /*! \brief The limits of the tickets: given per tick, chunks they keep loaded in all, and the
        loaded ticks a ticket lasts. \qcfields */
    struct Limits
    {
        qsizetype perTick = 4;
        qsizetype total = 256;
        quint64 ticks = 100;
    };

    /*! Sets \a limits. */
    void setLimits(const Limits &limits) { m_limits = limits; }
    /*! Returns the limits. */
    const Limits &limits() const { return m_limits; }

    /*! Gives \a chunk a ticket at tick \a now for a group of \a weight chunks. Returns \c false
        when the limits refuse it.
        A chunk with a ticket keeps it. A refusal counts unless \a retry (a wish refused before,
        asked again). */
    bool request(QcChunkPos chunk, quint64 now, qsizetype weight = 1, bool retry = false);
    /*! Returns whether tickets can be given at all (limits above zero). */
    bool canGive() const { return m_limits.perTick > 0 && m_limits.total > 0; }
    /*! \brief What a signal does in a chunk with a ticket: nothing, passes through (changes or
        scheduled ticks) or waits there for the next chunk of its way. */
    enum class Use {
        /*! No signal does anything there. */
        Free,
        /*! A signal passes through. */
        Busy,
        /*! A signal waits there for the next chunk of its way. */
        Waiting
    };
    /*! Ends the tickets that ran out at tick \a now; starts the budget of a new tick.
        \a isLoaded tells whether a chunk is loaded, \a use what a signal does in it. A ticket's
        time counts from the tick its chunk is loaded (reading it may take a while), so it lasts
        \c {ticks} loaded ticks. A signal is never taken into storage halfway (decision E-378):
        while one waits in the chunk the ticket stays, while one passes through it lasts up to
        \c {ticks} more after that. */
    void expire(quint64 now, const std::function<bool(QcChunkPos)> &isLoaded, const std::function<Use(QcChunkPos)> &use = {});
    /*! Returns whether \a chunk has a ticket. */
    bool contains(QcChunkPos chunk) const { return m_tickets.contains(chunk); }
    /*! Returns the chunks with a ticket. */
    QList<QcChunkPos> chunks() const { return m_tickets.keys(); }
    /*! Returns the number of tickets. */
    qsizetype count() const { return m_tickets.size(); }
    /*! Returns the chunks the tickets load, their groups counted whole. */
    qsizetype weight() const { return m_weight; }
    /*! Returns the tickets refused by the limits since the world opened. */
    quint64 refused() const { return m_refused; }
    /*! Returns the tickets given since the world opened. */
    quint64 given() const { return m_given; }
    /*! Returns the count of tickets given or ended (QcChunkLoading recomputes the needed chunks
        on a change). */
    quint64 changes() const { return m_changes; }
    /*! Ends every ticket. */
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
