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
