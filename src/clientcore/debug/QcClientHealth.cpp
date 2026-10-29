// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/debug/QcClientHealth.h"

#include "clientcore/debug/QcDebugSnapshot.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/tick/QcLinkStats.h"

namespace QcClientHealth {

QcHealthSample sample(const QcClientSession &session)
{
    QcHealthSample sample;
    const QcLinkStats::Taken link = session.linkStats().take();
    sample.roundTrip = link.roundTrip;
    sample.traffic = link.traffic;
    sample.latency = session.latency();
    if (session.state() != QcClientSession::State::Playing) {
        return sample;
    }

    const QcDebugSnapshot snapshot = QcDebugSnapshot::collect(session);
    const QcDebugSnapshot::Entities &entities = snapshot.entities;
    sample.loadedChunks = snapshot.loadedChunks;
    sample.entities = entities.mobs + entities.items + entities.fallingBlocks;
    sample.players = entities.players;
    if (snapshot.server) {
        sample.ticks = QcIntervalTiming{snapshot.server->tickMeanMs, snapshot.server->tickMaxMs, 0};
    }
    return sample;
}

} // namespace QcClientHealth
