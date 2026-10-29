// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcWorldTypeDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses the settings of the flat and void world types (worldgen/flat.json, worldgen/void.json,
// decision E-223).
namespace QcWorldTypeParser {

[[nodiscard]] QcResult<QcFlatDefinition> parseFlat(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcVoidDefinition> parseVoid(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWorldTypeParser
