// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWeather.h"

#include <QJsonObject>
#include <QtGlobal>

#include <optional>

/*! \brief The weather's course as a pure function of the world seed (decision E-395): after a
    clear spell comes rain or a thunderstorm, after them a clear spell again; the kind and length
    of each spell follow from the seed and the spell's number, so the same world has the same
    weather whatever happens in it, and /tick steps through it exactly. */
namespace QcWeatherCycle {

/*! Ticks a strength takes to fade fully in or out (10 s). */
inline constexpr int fadeTicks = 200;
/*! Lengths of the spells in ticks (20 per second): clear 5-25 min, rain 2.5-10 min, thunder 2-6
    min. \qcshared */
inline constexpr qint64 minClearTicks = 6'000;
inline constexpr qint64 maxClearTicks = 30'000;
inline constexpr qint64 minRainTicks = 3'000;
inline constexpr qint64 maxRainTicks = 12'000;
inline constexpr qint64 minThunderTicks = 2'400;
inline constexpr qint64 maxThunderTicks = 7'200;
/*! Chance in 100 that a spell after a clear one is a thunderstorm instead of rain. */
inline constexpr int thunderChance = 25;
/*! Longest spell /weather sets (one million seconds). */
inline constexpr qint64 maxSetTicks = 20'000'000;

/*! Returns the kind of spell number \a spell that follows a spell of \a previous in the world
    with \a seed. */
QcWeatherKind nextKind(qint64 seed, qint64 spell, QcWeatherKind previous);
/*! Returns the length in ticks of spell number \a spell of \a kind in the world with \a seed. */
qint64 lengthOf(qint64 seed, qint64 spell, QcWeatherKind kind);

/*! \brief The random draws of one tick for \c {purpose} (strikes, snowfall), from the seed and the
    tick the weather has reached: the same world draws the same in the same tick, whenever the
    server was started.

    A splitmix64 stream, set up anew each tick at the cost of one multiplication. */
class Draws
{
public:
    Draws(qint64 seed, const QcWeatherState &state, quint64 purpose);

    /*! Returns the next number in [\a lowest, \a highest) (\a highest > \a lowest). */
    int bounded(int lowest, int highest);
    /*! Returns the next number in [0, \a highest) (\a highest > 0). */
    int bounded(int highest) { return bounded(0, highest); }

private:
    quint64 m_state = 0;
};

/*! Returns the weather one tick after \a state in the world with \a seed: with \a cycle (game
    rule weatherCycle) the spell runs down and the next begins when it ends; the strengths fade
    towards the kind either way. */
[[nodiscard]] QcWeatherState next(QcWeatherState state, qint64 seed, bool cycle);
/*! Returns the weather of \a state with a spell of \a kind begun (/weather) in the world with
    \a seed: lasting \a ticks, or as long as the seed says. */
[[nodiscard]] QcWeatherState begun(QcWeatherState state, qint64 seed, QcWeatherKind kind, std::optional<qint64> ticks);

/*! Returns \a state as world.json's "weather" object. */
QJsonObject toJson(const QcWeatherState &state);
/*! Returns the weather read from world.json's "weather" object \a json; reading clamps every
    value into its range, a missing or damaged object reads as a world whose first spell has not
    begun. */
QcWeatherState fromJson(const QJsonObject &json);

} // namespace QcWeatherCycle
