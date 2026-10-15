#pragma once

#include "QcResult.h"
#include "QcWorldgenDefinitions.h"

#include <QJsonObject>

// Parses the world generation definitions of content packs: biomes, features, ores and terrain.
namespace QcWorldgenParser {

[[nodiscard]] QcResult<QcBiomeDefinition> parseBiome(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcOreDefinition> parseOre(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcTerrainSettings> parseTerrainSettings(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWorldgenParser
