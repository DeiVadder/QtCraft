// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcWorldgenDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses the world generation definitions of content packs: biomes, features, ores and
    terrain. */
namespace QcWorldgenParser {

/*! Returns the biome read from \a json, or the first error. Names are qualified with
    \a packNamespace. */
[[nodiscard]] QcResult<QcBiomeDefinition> parseBiome(const QJsonObject &json, const QString &packNamespace);
/*! Returns the feature read from \a json, or the first error. Names are qualified with
    \a packNamespace. */
[[nodiscard]] QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace);
/*! Returns the ore read from \a json, or the first error. Names are qualified with
    \a packNamespace. */
[[nodiscard]] QcResult<QcOreDefinition> parseOre(const QJsonObject &json, const QString &packNamespace);
/*! Returns the terrain settings read from \a json (worldgen/terrain.json) with its dungeons,
    mine shafts, coastal towers and freezing, or the first error. Names are qualified with
    \a packNamespace. */
[[nodiscard]] QcResult<QcTerrainSettings> parseTerrainSettings(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWorldgenParser
