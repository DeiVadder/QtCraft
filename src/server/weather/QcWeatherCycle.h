#pragma once

#include "common/world/QcWeather.h"

#include <QJsonObject>
#include <QtGlobal>

#include <optional>

// The weather of a world as world.json keeps it (decision E-395): the current spell (kind and the
// ticks until it ends), how many spells the world has had, the strengths of rain and thunder,
// which fade towards the spell's kind, and how many ticks the weather has run.
struct QcWeatherState
{
    QcWeatherKind kind = QcWeatherKind::Clear;
    // Ticks until the spell ends while the game rule weatherCycle is on.
    qint64 ticksLeft = 0;
    // Spells so far; with the seed it decides the next spell. 0: the world's first spell has not
    // begun (a new world, or one saved without weather), the first tick starts a clear one.
    qint64 spell = 0;
    // Strengths in 0..1.
    float rain = 0.0f;
    float thunder = 0.0f;
    // Ticks the weather has run (world.json "ticks", 0 when missing). With the seed it decides the
    // random draws of each tick (strikes, snowfall; QcWeatherCycle::Draws), so they go on after
    // loading as if the world had not been saved (decision E-397).
    qint64 ticks = 0;

    friend bool operator==(const QcWeatherState &, const QcWeatherState &) = default;
};

// The weather's course as a pure function of the world seed (decision E-395): after a clear spell
// comes rain or a thunderstorm, after them a clear spell again; the kind and length of each spell
// follow from the seed and the spell's number, so the same world has the same weather whatever
// happens in it, and /tick steps through it exactly.
namespace QcWeatherCycle {

// Ticks a strength takes to fade fully in or out (10 s).
inline constexpr int fadeTicks = 200;
// Lengths of the spells in ticks (20 per second): clear 5-25 min, rain 2.5-10 min, thunder 2-6 min.
inline constexpr qint64 minClearTicks = 6'000;
inline constexpr qint64 maxClearTicks = 30'000;
inline constexpr qint64 minRainTicks = 3'000;
inline constexpr qint64 maxRainTicks = 12'000;
inline constexpr qint64 minThunderTicks = 2'400;
inline constexpr qint64 maxThunderTicks = 7'200;
// Chance in 100 that a spell after a clear one is a thunderstorm instead of rain.
inline constexpr int thunderChance = 25;
// Longest spell /weather sets (one million seconds).
inline constexpr qint64 maxSetTicks = 20'000'000;

// The kind of spell number `spell` that follows a spell of `previous`.
QcWeatherKind nextKind(qint64 seed, qint64 spell, QcWeatherKind previous);
// The length of spell number `spell` of `kind`.
qint64 lengthOf(qint64 seed, qint64 spell, QcWeatherKind kind);

// The random draws of one tick for `purpose` (strikes, snowfall), from the seed and the tick the
// weather has reached: the same world draws the same in the same tick, whenever the server was
// started. A splitmix64 stream, set up anew each tick at the cost of one multiplication.
class Draws
{
public:
    Draws(qint64 seed, const QcWeatherState &state, quint64 purpose);

    // A number in [lowest, highest) (highest > lowest).
    int bounded(int lowest, int highest);
    // A number in [0, highest) (highest > 0).
    int bounded(int highest) { return bounded(0, highest); }

private:
    quint64 m_state = 0;
};

// The weather one tick after `state`: with `cycle` (game rule weatherCycle) the spell runs down and
// the next begins when it ends; the strengths fade towards the kind either way.
[[nodiscard]] QcWeatherState next(QcWeatherState state, qint64 seed, bool cycle);
// The weather of `state` with a spell of `kind` begun (/weather): lasting `ticks`, or as long as the
// seed says.
[[nodiscard]] QcWeatherState begun(QcWeatherState state, qint64 seed, QcWeatherKind kind, std::optional<qint64> ticks);

// world.json's "weather" object; reading clamps every value into its range, a missing or damaged
// object reads as a world whose first spell has not begun.
QJsonObject toJson(const QcWeatherState &state);
QcWeatherState fromJson(const QJsonObject &json);

} // namespace QcWeatherCycle
