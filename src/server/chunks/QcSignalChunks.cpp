// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/chunks/QcSignalChunks.h"

#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/chunks/QcChunkTickets.h"
#include "server/core/QcServerState.h"

#include <algorithm>

QcSignalChunks::QcSignalChunks(QcServerState &state, const QcChunkTickets &tickets, const QcBlockUpdateQueue &updates)
    : m_state(state)
    , m_tickets(tickets)
    , m_updates(updates)
{
}

QSet<QcChunkPos> QcSignalChunks::targets() const
{
    QSet<QcChunkPos> targets;
    const QcCircuitState &circuits = m_state.circuits();
    for (auto it = circuits.waitingOn.cbegin(); it != circuits.waitingOn.cend(); ++it) {
        const bool signal = std::ranges::any_of(*it, [&circuits](QcBlockPos pos) { return circuits.signalWaits.contains(pos); });
        if (signal && !m_state.world().chunk(it.key())) {
            targets.insert(it.key());
        }
    }
    return targets;
}

QSet<QcChunkPos> QcSignalChunks::chunksWithSignals() const
{
    QSet<QcChunkPos> chunks;
    for (const QcCircuitState::Change &change : std::as_const(m_state.circuits().changes)) {
        if (change.signal) {
            chunks.insert(change.pos.chunk());
        }
    }
    return chunks;
}

QSet<QcChunkPos> QcSignalChunks::waitingChunks() const
{
    // A signal waiting for a chunk that has not arrived yet keeps its own chunk, so it can go on when
    // that one comes: the chunk has a ticket or asks for one again each tick (decisions E-376, E-378).
    QSet<QcChunkPos> chunks;
    const QcCircuitState &circuits = m_state.circuits();
    for (auto it = circuits.waitingOn.cbegin(); it != circuits.waitingOn.cend(); ++it) {
        if ((!m_tickets.contains(it.key()) && !m_tickets.canGive()) || m_state.world().chunk(it.key())) {
            continue;
        }
        for (const QcBlockPos &pos : *it) {
            if (circuits.signalWaits.contains(pos)) {
                chunks.insert(pos.chunk());
            }
        }
    }
    return chunks;
}

QSet<QcChunkPos> QcSignalChunks::passing(const QSet<QcChunkPos> &candidates, const QSet<QcChunkPos> &circuitChunks)
{
    // A chunk the circuits kept until this tick without a ticket of its own (with the group of one,
    // or held by a signal that waited there) would take a signal that runs on now into storage
    // halfway, and nothing would bring it back. It stays while the signal passes (changes,
    // scheduled ticks, block updates the switched components caused), at most as long as a ticket
    // lasts, so a clock does not keep its chunk for ever. Chunks a player leaves behind go as they
    // are; their machines resume when they come back (decision E-374).
    if (candidates.isEmpty()) {
        m_passingSince.clear();
        return {};
    }
    QSet<QcChunkPos> passing = chunksWithSignals();
    for (const QList<QcBlockPos> &updates : {m_updates.playerUpdates(), m_updates.commandUpdates()}) {
        for (const QcBlockPos &pos : updates) {
            passing.insert(pos.chunk());
        }
    }

    const quint64 now = m_state.clock().tick();
    const quint64 limit = static_cast<quint64>(m_state.config().limits.chunkTicketTicks);
    const QcSignalSchedule &schedule = m_state.blockEntities().schedule();
    QHash<QcChunkPos, quint64> since;
    QSet<QcChunkPos> held;
    // Every candidate is judged on its own, so the order does not matter.
    for (const QcChunkPos &chunk : candidates) {
        const bool forCircuits = circuitChunks.contains(chunk) || m_passingSince.contains(chunk);
        if (!forCircuits || (!passing.contains(chunk) && !schedule.hasEntriesIn(chunk))) {
            continue;
        }

        const quint64 start = m_passingSince.value(chunk, now);
        since.insert(chunk, start);
        if (now - start < limit) {
            held.insert(chunk);
        }
    }
    m_passingSince = std::move(since);
    return held;
}

void QcSignalChunks::forget(QcChunkPos chunk)
{
    m_passingSince.remove(chunk);
}
