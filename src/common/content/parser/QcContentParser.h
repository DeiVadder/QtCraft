#pragma once

#include "QcBlockParser.h"
#include "QcCommandMacroParser.h"
#include "QcHandbookChapter.h"
#include "QcItemParser.h"
#include "QcMaterialParser.h"
#include "QcMobParser.h"
#include "QcRecipeParser.h"
#include "QcResult.h"
#include "QcShapeParser.h"
#include "QcShowcaseParser.h"
#include "QcSoundParser.h"
#include "QcStructureParser.h"
#include "QcTextureDefinition.h"
#include "QcWeatherParser.h"
#include "QcWorldgenParser.h"
#include "QcWorldTypeParser.h"

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

[[nodiscard]] QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json);
[[nodiscard]] QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace);

} // namespace QcContentParser
