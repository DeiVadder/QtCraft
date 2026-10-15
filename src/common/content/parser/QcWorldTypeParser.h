#pragma once

#include "QcResult.h"
#include "QcWorldTypeDefinitions.h"

#include <QJsonObject>

// Parses the settings of the flat and void world types (worldgen/flat.json, worldgen/void.json,
// decision E-223).
namespace QcWorldTypeParser {

[[nodiscard]] QcResult<QcFlatDefinition> parseFlat(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcVoidDefinition> parseVoid(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWorldTypeParser
