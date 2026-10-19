#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

struct QcBiomeDefinition;

// What the sky does in the whole world (decision E-395): clear, rain (snow where it is cold) or a
// thunderstorm, which rains as well.
enum class QcWeatherKind : quint8 { Clear, Rain, Thunder };

// What falls at one place while it rains: nothing (a dry biome, or no rain at all), rain or snow.
enum class QcPrecipitation : quint8 { None, Rain, Snow };

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

namespace QcWeather {

// The name used by /weather, world.json and the control interface ("clear", "rain", "thunder").
QString name(QcWeatherKind kind);
[[nodiscard]] std::optional<QcWeatherKind> parse(QStringView name);

// Biomes at least this warm stay dry in rain (desert).
inline constexpr double dryTemperature = 0.9;
// Biomes colder than this get snow instead of rain (taiga, tundra).
inline constexpr double snowTemperature = 0.25;
// Above this height it snows in every biome that is not dry; a biome with a lower snow line
// ("snowLine", mountains) snows from there up.
inline constexpr int snowHeight = 150;
// snowFrom of a climate in which it never snows.
inline constexpr int noSnow = 32767;

// The precipitation of one column: dry, or rain below `snowFrom` and snow from that height up.
struct Climate
{
    bool dry = false;
    int snowFrom = snowHeight;

    friend bool operator==(const Climate &, const Climate &) = default;
};

// The climate of a column of `biome`; without a biome (flat, void and showcase worlds) it rains up
// to snowHeight.
Climate climateOf(const QcBiomeDefinition *biome);
// What falls at height `y` of a column with `climate` while it rains.
QcPrecipitation precipitationAt(Climate climate, int y);

} // namespace QcWeather
