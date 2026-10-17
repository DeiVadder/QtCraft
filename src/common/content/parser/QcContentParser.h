#pragma once

#include "common/content/definitions/QcTextureDefinition.h"
#include "common/content/parser/QcBlockParser.h"
#include "common/content/parser/QcCommandMacroParser.h"
#include "common/content/parser/QcItemParser.h"
#include "common/content/parser/QcMaterialParser.h"
#include "common/content/parser/QcMobParser.h"
#include "common/content/parser/QcRecipeParser.h"
#include "common/content/parser/QcShapeParser.h"
#include "common/content/parser/QcShowcaseParser.h"
#include "common/content/parser/QcSoundParser.h"
#include "common/content/parser/QcStructureParser.h"
#include "common/content/parser/QcTradeParser.h"
#include "common/content/parser/QcVillageParser.h"
#include "common/content/parser/QcVillagerParser.h"
#include "common/content/parser/QcWeatherParser.h"
#include "common/content/parser/QcWorldgenParser.h"
#include "common/content/parser/QcWorldTypeParser.h"
#include "common/core/QcResult.h"
#include "common/handbook/QcHandbookChapter.h"

#include <QHash>
#include <QJsonObject>

// Turns content-pack JSON documents into definitions. Unqualified names get the pack's namespace.
// All input is untrusted: every field is type-checked and range-checked.
namespace QcContentParser {

using QcBlockParser::parseBlock;
using QcCommandMacroParser::parseCommandMacro;
using QcShapeParser::parseShape;
using QcMaterialParser::parsePartFamily;
using QcMaterialParser::parseMaterial;
using QcMaterialParser::partRecipeJson;
using QcMobParser::parseMob;
using QcMobParser::parseModel;
using QcMobParser::parseAppearance;
using QcRecipeParser::parseRecipe;
using QcItemParser::parseItem;
using QcRecipeParser::parseSmeltingRecipe;
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
using QcVillagerParser::parseVillagerSettings;
using QcTradeParser::parseTradeSet;

[[nodiscard]] QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json);
[[nodiscard]] QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace);

} // namespace QcContentParser
