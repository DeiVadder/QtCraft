// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcWeather.h"

#include "common/content/definitions/QcWorldgenDefinitions.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcWeather {

QString name(QcWeatherKind kind)
{
    switch (kind) {
    case QcWeatherKind::Clear:
        return u"clear"_s;
    case QcWeatherKind::Rain:
        return u"rain"_s;
    case QcWeatherKind::Thunder:
        return u"thunder"_s;
    }
    return u"clear"_s;
}

std::optional<QcWeatherKind> parse(QStringView name)
{
    for (const QcWeatherKind kind : {QcWeatherKind::Clear, QcWeatherKind::Rain, QcWeatherKind::Thunder}) {
        if (name == QcWeather::name(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

Climate climateOf(const QcBiomeDefinition *biome)
{
    if (!biome) {
        return {};
    }
    if (biome->temperature >= dryTemperature) {
        return {true, noSnow};
    }
    if (biome->temperature < snowTemperature) {
        return {false, -noSnow};
    }

    const int snowLine = biome->snowLine > 0 ? biome->snowLine : snowHeight;
    return {false, std::min(snowLine, snowHeight)};
}

QcPrecipitation precipitationAt(Climate climate, int y)
{
    if (climate.dry) {
        return QcPrecipitation::None;
    }
    return y >= climate.snowFrom ? QcPrecipitation::Snow : QcPrecipitation::Rain;
}

} // namespace QcWeather
