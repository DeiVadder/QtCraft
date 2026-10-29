// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcMessages.h"
#include "common/world/QcPrecipitationTable.h"
#include "common/world/QcWeather.h"
#include "server/weather/QcWeatherCycle.h"

#include <QHash>

#include <functional>
#include <optional>

class QcServerState;
struct QcServerClient;

// The weather in the server tick (decision E-395). The world's spell runs its course by the seed
// (QcWeatherCycle, game rule weatherCycle), its strengths fade in and out, and every player gets
// the weather where it stands (QcMsg::Weather) when it joins and whenever that changes. Where rain
// or snow falls follows from the column's biome (QcWeather::climateOf) and whether the sky is open
// above the position (nothing above it that stops precipitation, QcPrecipitationTable).
//
// During a thunderstorm lightning strikes near the players now and then (the top of a column in the
// rain); every player within hearing range gets a QcMsg::Lightning, and the strike hook does what a
// strike does to the world.
class QcServerWeather
{
public:
    // What a strike does to the world at `pos` (the cell above the column's top); unset, nothing.
    using StrikeHook = std::function<void(QcBlockPos pos)>;

    // Rain counts from this strength on (precipitationAt); below it the first drops only darken the sky.
    static constexpr float rainThreshold = 0.2f;
    // Lightning strikes while the thunder's strength is at least this.
    static constexpr float thunderThreshold = 0.5f;
    // Strikes fall within this many blocks of the player (each axis).
    static constexpr int strikeReach = 48;
    // Players within this many blocks (horizontally) of a strike see and hear it.
    static constexpr double strikeHearing = 256.0;
    // A sent strength is sent again when it changed by this much (or reached 0 or 1).
    static constexpr float sendStep = 0.05f;

    explicit QcServerWeather(QcServerState &state);

    // One tick of a simulated world: the course, strikes and what the players are sent.
    void tick();
    // The weather where the player stands, sent now (joining).
    void sendTo(QcServerClient &client);

    const QcWeatherState &state() const;
    // Begins a spell of `kind` lasting `ticks` or as long as the seed says (/weather).
    void set(QcWeatherKind kind, std::optional<qint64> ticks);

    // The climate of the column (x, z) from its biome.
    QcWeather::Climate climateAt(int x, int z) const;
    // Highest cell of the column (x, z) that keeps rain and snow out; QcWorld::worldHeight for a
    // column that is not loaded.
    int columnTop(int x, int z) const;
    // Whether nothing stops rain coming straight down above `pos` (a loaded column whose top lies
    // below it).
    bool openToSky(QcBlockPos pos) const { return pos.y > columnTop(pos.x, pos.z); }
    // What falls on `pos` now: nothing while it does not rain (strength below rainThreshold), in a
    // dry biome or under a roof; otherwise rain or snow by the climate and the height.
    QcPrecipitation precipitationAt(QcBlockPos pos) const;
    bool isRainingAt(QcBlockPos pos) const { return precipitationAt(pos) == QcPrecipitation::Rain; }
    bool isSnowingAt(QcBlockPos pos) const { return precipitationAt(pos) == QcPrecipitation::Snow; }

    // Lightning strikes the column top `pos`: the players within strikeHearing are told, then the
    // strike hook runs.
    void strike(QcBlockPos pos);
    void setStrikeHook(StrikeHook hook) { m_strikeHook = std::move(hook); }
    // Strikes since the server started (tests, measurements).
    quint64 strikes() const { return m_strikes; }
    // On average one strike per player every this many ticks of a thunderstorm (lightning
    // meanSeconds of the content's weather, decision E-396).
    int meanStrikeTicks() const { return m_meanStrikeTicks; }

private:
    // Purpose of the strikes' draws (QcWeatherCycle::Draws).
    static constexpr quint64 strikeRandom = 1;

    QcMsg::Weather messageFor(const QcServerClient &client) const;
    // Whether `next` differs enough from what the client got to be sent.
    static bool worthSending(const std::optional<QcMsg::Weather> &sent, QcMsg::Weather next);
    void syncClients();
    // During a thunderstorm: maybe one strike near each player.
    void strikeNearPlayers();

    QcServerState &m_state;
    QcPrecipitationTable m_table;
    StrikeHook m_strikeHook;
    quint64 m_strikes = 0;
    int m_meanStrikeTicks = 1;
    // Climate by column, filled on demand (the biome of a column never changes), in two
    // generations: when the recent one is full it becomes the older one and the oldest is dropped;
    // a column found in the older one moves back to the recent one. So the columns around the
    // players stay cached while they move, and the cache holds at most 2 × climateGeneration.
    static constexpr qsizetype climateGeneration = 32768;
    mutable QHash<quint64, QcWeather::Climate> m_climates;
    mutable QHash<quint64, QcWeather::Climate> m_olderClimates;
};
