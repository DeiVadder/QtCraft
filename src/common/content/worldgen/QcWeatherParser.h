// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcWeatherDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses what the weather does to the world (weather.json of a pack, decision E-396). */
namespace QcWeatherParser {

/*! Returns the weather definition read from \a json, or the first error; missing fields keep
    their defaults. The snow layer block is qualified with \a packNamespace. */
[[nodiscard]] QcResult<QcWeatherDefinition> parseWeather(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWeatherParser
