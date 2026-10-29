// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/maps/QcClientMaps.h"
#include "clientcore/session/QcBlockMirror.h"
#include "clientcore/session/QcChunkExtras.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/weather/QcClientWeather.h"
#include "common/protocol/QcMessages.h"

#include <QList>

#include <memory>
#include <optional>
#include <utility>

// The world as the server sends it (part of QcClientSession, decision E-717): the content, the blocks
// of the loaded chunks with the local player's predicted changes on top (QcBlockMirror, decision
// E-853; QcClientWorld, there from the content on), what the chunks carry besides their blocks
// (QcChunkExtras), the world time and the weather. It takes the server's world messages and tells
// what they changed; the session reports that through its signals.
class QcWorldMirror
{
public:
    // What a world message changed.
    using Change = QcBlockMirror::Change;

    // The change a world message made, none for a message of another group; `eye` hears the
    // thunder of a lightning strike, `now` is the client's tick count.
    [[nodiscard]] std::optional<Change> handle(const QcMessage &message, const QcVec3 &eye, quint64 now);
    // One client tick: explosions fade, the world time runs on while the time of day runs.
    void tick(quint64 now);

    // Shows the blocks the local player's action numbered `sequence` is expected to set and returns the
    // cells that changed (QcBlockMirror::predict).
    QList<QcBlockMirror::Changed> predict(quint32 sequence, const QList<QcMsg::BlockUpdate> &blocks) { return m_blocks.predict(sequence, blocks); }
    // The blocks with the predictions.
    const QcBlockMirror &blocks() const { return m_blocks; }

    // Null until the content arrived.
    const QcClientWorld *world() const { return m_blocks.world(); }
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_blocks.content(); }
    // Seconds of world time, advanced locally between server updates while the time of day runs, by
    // timeSpeed() per tick (the server's tick rate or sprint, WorldTime::speed).
    double worldTime() const { return m_worldTime.time; }
    double timeSpeed() const { return m_worldTime.speed; }
    // The weather as shown: strengths, climate, lightning flashes (decision E-395).
    QcClientWeather &weather() { return m_weather; }
    const QcClientWeather &weather() const { return m_weather; }
    // Labels (E-222), spinning spawner creatures (E-520) and explosions (E-604) of the loaded chunks.
    const QcChunkExtras &extras() const { return m_extras; }
    // The maps the server sent (decision E-802).
    const QcClientMaps &maps() const { return m_maps; }

private:
    // Labels, spawners, explosions, maps, time and weather; false for other messages.
    bool handleSkyMessage(const QcMessage &message, const QcVec3 &eye, quint64 now);

    QcBlockMirror m_blocks;
    QcChunkExtras m_extras;
    QcClientMaps m_maps;
    // The latest world time from the server.
    QcMsg::WorldTime m_worldTime{0.0, 600.0, true, 1.0};
    QcClientWeather m_weather;
};
