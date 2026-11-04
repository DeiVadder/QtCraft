// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcMessages.h"
#include "common/world/QcPrecipitationTable.h"
#include "common/world/QcWeather.h"
#include "server/dimensions/QcPerDimension.h"
#include "server/weather/QcWeatherCycle.h"

#include <QHash>

#include <functional>
#include <optional>

class QcServerState;
struct QcServerClient;

/*! \brief The weather in the server tick (decision E-395).

    The world's spell runs its course by the seed (QcWeatherCycle, game rule weatherCycle), its
    strengths fade in and out, and every player gets the weather where it stands (QcMsg::Weather)
    when it joins and whenever that changes. Where rain or snow falls follows from the column's
    biome (QcWeather::climateOf) and whether the sky is open above the position (nothing above it
    that stops precipitation, QcPrecipitationTable).

    During a thunderstorm lightning strikes near the players now and then (the top of a column in
    the rain); every player within hearing range gets a QcMsg::Lightning, and the strike hook
    does what a strike does to the world. */
class QcServerWeather
{
public:
    /*! What a strike does to the world at \c {pos} (the cell above the column's top); unset,
        nothing. */
    using StrikeHook = std::function<void(QcBlockPos pos)>;

    /*! Rain counts from this strength on (precipitationAt); below it the first drops only darken
        the sky. */
    static constexpr float rainThreshold = 0.2f;
    /*! Lightning strikes while the thunder's strength is at least this. */
    static constexpr float thunderThreshold = 0.5f;
    /*! Strikes fall within this many blocks of the player (each axis). */
    static constexpr int strikeReach = 48;
    /*! Players within this many blocks (horizontally) of a strike see and hear it. */
    static constexpr double strikeHearing = 256.0;
    /*! A sent strength is sent again when it changed by this much (or reached 0 or 1). */
    static constexpr float sendStep = 0.05f;

    explicit QcServerWeather(QcServerState &state);

    /*! One tick of the world's weather: its course, once for all dimensions. */
    void advance();
    /*! One tick of a simulated dimension: strikes and what its players are sent. */
    void tick();
    /*! Sends \a client the weather where its player stands, now (joining). */
    void sendTo(QcServerClient &client);

    /*! Returns the weather in the current dimension (spell, ticks left, strengths): the world's,
        or none in a dimension without weather. */
    const QcWeatherState &state() const;
    /*! Begins a spell of \a kind lasting \a ticks or as long as the seed says (/weather). */
    void set(QcWeatherKind kind, std::optional<qint64> ticks);

    /*! Returns the climate of the column (\a x, \a z) from its biome. */
    QcWeather::Climate climateAt(int x, int z) const;
    /*! Returns the highest cell of the column (\a x, \a z) that keeps rain and snow out;
        QcWorld::worldHeight for a column that is not loaded. */
    int columnTop(int x, int z) const;
    /*! Returns whether nothing stops rain coming straight down above \a pos (a loaded column
        whose top lies below it). */
    bool openToSky(QcBlockPos pos) const { return pos.y > columnTop(pos.x, pos.z); }
    /*! Returns what falls on \a pos now: nothing while it does not rain (strength below
        rainThreshold), in a dry biome or under a roof; otherwise rain or snow by the climate and
        the height. */
    QcPrecipitation precipitationAt(QcBlockPos pos) const;
    /*! Returns whether rain falls on \a pos now (precipitationAt()). */
    bool isRainingAt(QcBlockPos pos) const { return precipitationAt(pos) == QcPrecipitation::Rain; }
    /*! Returns whether snow falls on \a pos now (precipitationAt()). */
    bool isSnowingAt(QcBlockPos pos) const { return precipitationAt(pos) == QcPrecipitation::Snow; }

    /*! Lightning strikes the column top \a pos: the players within strikeHearing are told, then
        the strike hook runs. */
    void strike(QcBlockPos pos);
    /*! Sets \a hook as what a strike does to the world. */
    void setStrikeHook(StrikeHook hook) { m_strikeHook = std::move(hook); }
    /*! Returns the strikes since the server started (tests, measurements). */
    quint64 strikes() const { return m_strikes; }
    /*! Returns the mean time between strikes: on average one strike per player every this many
        ticks of a thunderstorm (lightning meanSeconds of the content's weather, decision E-396). */
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
    // The climates of the columns of a dimension (the generators differ).
    struct Climates
    {
        QHash<quint64, QcWeather::Climate> climates;
        QHash<quint64, QcWeather::Climate> olderClimates;
    };
    // Of the current dimension.
    Climates &here() const;

    QcPerDimension<Climates> m_perDimension;
};
