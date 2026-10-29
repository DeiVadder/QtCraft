// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>
#include <QSet>

class QcBlockUpdateQueue;
class QcChunkTickets;
class QcServerState;

// The chunks the signals of the circuits concern (decisions E-376, E-378, E-410), for
// QcChunkLoading: the chunks signals wait for that are not loaded, the chunks of signals waiting for
// one on its way, the chunks with signals on their way, and the unneeded chunks a signal passes
// through, held for at most the time of a load ticket.
class QcSignalChunks
{
public:
    QcSignalChunks(QcServerState &state, const QcChunkTickets &tickets, const QcBlockUpdateQueue &updates);

    // The chunks signals wait for that are not loaded.
    [[nodiscard]] QSet<QcChunkPos> targets() const;
    // The chunks with signals on their way that the circuits look at next.
    [[nodiscard]] QSet<QcChunkPos> chunksWithSignals() const;
    // The chunks of signals that wait for a chunk on its way (they stay, decision E-376).
    [[nodiscard]] QSet<QcChunkPos> waitingChunks() const;
    // The chunks of `candidates` (unneeded, about to leave) that a signal passes through (changes,
    // scheduled ticks, block updates) and that the circuits kept (`circuitChunks`, at the last pass)
    // or that were held already: they stay for at most the time of a ticket (decision E-410).
    [[nodiscard]] QSet<QcChunkPos> passing(const QSet<QcChunkPos> &candidates, const QSet<QcChunkPos> &circuitChunks);
    // A chunk left the world: back in it, a signal it took along is held again for a ticket's time.
    void forget(QcChunkPos chunk);

private:
    QcServerState &m_state;
    const QcChunkTickets &m_tickets;
    const QcBlockUpdateQueue &m_updates;
    // Unneeded chunks a signal passes through, since the tick they are held for it (E-410).
    QHash<QcChunkPos, quint64> m_passingSince;
};
