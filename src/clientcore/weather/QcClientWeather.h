// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSoundCue.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcMessages.h"
#include "common/world/QcWeather.h"

#include <QList>

#include <optional>

// The weather as the client shows it (decision E-395): the strengths of rain and thunder the server
// sent, eased so a step of a message never jumps; the climate where the player stands; the flash of
// the latest lightning; and the thunder still on its way (sound travels 343 m/s, so a strike 170 m
// away is heard half a second after its flash). GUI-free; the session ticks it.
class QcClientWeather
{
public:
    // Speed of sound in metres per second.
    static constexpr double speedOfSound = 343.0;
    // The shown strengths move at most this much per tick towards the received ones (twice the
    // server's fade, so they keep up with it).
    static constexpr float easePerTick = 0.01f;
    // A flash lights the sky for this many ticks.
    static constexpr int flashTicks = 12;
    // Thunder further away than this is placed at this distance (towards the strike) and quieter
    // by the ratio, so it stays within the reach of its sound (QcSoundDefinition::maxRange).
    static constexpr double thunderPlacement = 96.0;
    // Thunder from strikes further away than this is not heard at all.
    static constexpr double thunderHearing = 256.0;

    void receive(QcMsg::Weather weather);
    // A strike seen by a listener at `listener` in client tick `tick`: the flash begins, the thunder
    // waits for its time.
    void lightning(const QcMsg::Lightning &strike, const QcVec3 &listener, quint64 tick);
    // One client tick: eases the strengths and returns the thunder due now.
    QList<QcSoundCue> tick(quint64 tick);

    float rain() const { return m_rain; }
    float thunder() const { return m_thunder; }
    const QcWeather::Climate &climate() const { return m_received.climate; }
    // What falls at height `y` near the player while it rains (QcWeather::precipitationAt).
    QcPrecipitation precipitationAt(int y) const;
    // Rain strength falling at height `y` near the player: 0 where it snows or stays dry.
    float rainAt(int y) const { return precipitationAt(y) == QcPrecipitation::Rain ? m_rain : 0.0f; }
    // Brightness of the lightning flash in 0..1 at `ticks` (the client tick with its fraction):
    // a bright stroke, a short dark gap, a second stroke, then fading out.
    float flash(double ticks) const;
    // Strikes seen since the session began (tests, control interface).
    quint64 strikes() const { return m_strikes; }

private:
    struct Thunder
    {
        quint64 dueTick = 0;
        QcSoundCue cue;
    };

    QcMsg::Weather m_received;
    float m_rain = 0.0f;
    float m_thunder = 0.0f;
    // Tick of the latest flash; none before the first strike.
    std::optional<quint64> m_flashTick;
    QList<Thunder> m_thunderOnItsWay;
    quint64 m_strikes = 0;
};
