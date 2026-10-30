// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockParser.h"
#include "common/content/blocks/QcMaterialParser.h"
#include "common/content/blocks/QcShapeParser.h"
#include "common/content/blocks/QcTextureDefinition.h"
#include "common/content/items/QcEffectParser.h"
#include "common/content/items/QcEnchantmentParser.h"
#include "common/content/items/QcHoldParser.h"
#include "common/content/items/QcItemParser.h"
#include "common/content/items/QcProjectileParser.h"
#include "common/content/mobs/QcMobParser.h"
#include "common/content/recipes/QcLootTableParser.h"
#include "common/content/recipes/QcRecipeParser.h"
#include "common/content/registry/QcAdvancementParser.h"
#include "common/content/registry/QcCommandMacroParser.h"
#include "common/content/registry/QcSoundParser.h"
#include "common/content/villages/QcTradeParser.h"
#include "common/content/villages/QcVillageParser.h"
#include "common/content/villages/QcVillagerParser.h"
#include "common/content/worldgen/QcShowcaseParser.h"
#include "common/content/worldgen/QcStructureParser.h"
#include "common/content/worldgen/QcWeatherParser.h"
#include "common/content/worldgen/QcWorldgenParser.h"
#include "common/content/worldgen/QcWorldTypeParser.h"
#include "common/core/QcResult.h"
#include "common/handbook/QcHandbookChapter.h"

#include <QHash>
#include <QJsonObject>

/*! \brief Turns content-pack JSON documents into definitions.

    Unqualified names get the pack's namespace. All input is untrusted: every field is
    type-checked and range-checked. */
namespace QcContentParser {

using QcBlockParser::parseBlock;
using QcCommandMacroParser::parseCommandMacro;
using QcEffectParser::parseEffect;
using QcEnchantmentParser::parseEnchantment;
using QcAdvancementParser::parseAdvancement;
using QcLootTableParser::parseLootTable;
using QcShapeParser::parseShape;
using QcMaterialParser::parsePartFamily;
using QcMaterialParser::parseMaterial;
using QcMaterialParser::partRecipeJson;
using QcMobParser::parseMob;
using QcMobParser::parseModel;
using QcMobParser::parseAppearance;
using QcRecipeParser::parseRecipe;
using QcItemParser::parseItem;
using QcHoldParser::parseHoldDefaults;
using QcProjectileParser::parseProjectile;
using QcRecipeParser::parseSmeltingRecipe;
using QcRecipeParser::parseStonecuttingRecipe;
using QcShowcaseParser::parseShowcase;
using QcSoundParser::parseSound;
using QcStructureParser::parseStructure;
using QcWorldgenParser::parseBiome;
using QcWorldgenParser::parseFeature;
using QcWorldgenParser::parseOre;
using QcWorldgenParser::parseTerrainSettings;
using QcWorldTypeParser::parseFlat;
using QcWorldTypeParser::parseVoid;
using QcWeatherParser::parseWeather;
using QcVillageParser::parseVillageSettings;
using QcVillageParser::parseVillageStyle;
using QcVillagerParser::parseProfession;
using QcVillagerParser::parseVillagerSettings;
using QcTradeParser::parseTradeSet;

/*! Returns the procedural texture described by \a json, with an unqualified name in
    \a packNamespace, or the first problem found. Only the fields the texture synthesizer reads
    are kept. */
[[nodiscard]] QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace);
/*! Returns the tiles of \a texture when it is larger than one block (decision E-791, "tiles":
    [width, height], 1 to QcPaintingMotif::maxSide each): one texture per block named
    "<name>_<column>_<row>" with its place as "tile": [column, row]; empty for a texture of one
    block. */
[[nodiscard]] QList<QcTextureDefinition> textureTiles(const QcTextureDefinition &texture);
/*! Returns the key-text pairs of the language file \a json, or an error when a value is no
    string or the file holds more than QcStreamLimits::maxDefinitions entries. */
[[nodiscard]] QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json);
/*! Returns the handbook chapter described by \a json, with unqualified names in
    \a packNamespace, or the first problem found. */
[[nodiscard]] QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace);

} // namespace QcContentParser
