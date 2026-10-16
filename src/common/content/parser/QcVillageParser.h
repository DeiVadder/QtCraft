#pragma once

#include "QcResult.h"
#include "QcVillageDefinitions.h"

#include <QJsonObject>

// Parses village styles (villages/*.json) and the village settings (worldgen/villages.json),
// decision E-403.
namespace QcVillageParser {

[[nodiscard]] QcResult<QcVillageStyleDefinition> parseVillageStyle(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace);

} // namespace QcVillageParser
