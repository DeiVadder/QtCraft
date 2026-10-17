#pragma once

#include "common/content/definitions/QcVillageDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses village styles (villages/*.json) and the village settings (worldgen/villages.json),
// decision E-403. A style with "extends" (decision E-421) starts from the style it names: without
// its own "name" it patches that style, with one it is a new style (without the base's biomes and
// fallback unless it gives them). It replaces the base's fields it gives, takes the biomes of its
// "remove" object and appends the lists of its "add" object (biomes, crops, pieces, layouts).
namespace QcVillageParser {

[[nodiscard]] QcResult<QcVillageStyleDefinition> parseVillageStyle(const QJsonObject &json, const QString &packNamespace);
// The style a file extends (qualified); an error when "extends" is no valid name.
[[nodiscard]] QcResult<QString> extendedStyle(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcVillageStyleDefinition> extendVillageStyle(const QJsonObject &json, const QString &packNamespace,
                                                                    const QcVillageStyleDefinition &base);
[[nodiscard]] QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace);
// The settings of a later pack read over `settings` (the earlier packs'): the fields it gives
// replace theirs, `golem` field by field (decision E-421).
[[nodiscard]] QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace,
                                                               QcVillageSettings settings);

} // namespace QcVillageParser
