// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcWeatherDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses what the weather does to the world (weather.json of a pack, decision E-396).
namespace QcWeatherParser {

[[nodiscard]] QcResult<QcWeatherDefinition> parseWeather(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWeatherParser
