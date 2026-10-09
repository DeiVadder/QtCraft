#pragma once

#include "QcBlockParser.h"
#include "QcHandbookChapter.h"
#include "QcItemParser.h"
#include "QcMaterialParser.h"
#include "QcMobParser.h"
#include "QcRecipeParser.h"
#include "QcResult.h"
#include "QcShapeParser.h"
#include "QcSoundParser.h"
#include "QcTextureDefinition.h"
#include "QcWorldgenParser.h"

#include <QHash>
#include <QJsonObject>

// Turns content-pack JSON documents into definitions. Unqualified names get the pack's namespace.
// All input is untrusted: every field is type-checked and range-checked.
namespace QcContentParser {

using QcBlockParser::parseBlock;
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
using QcSoundParser::parseSound;
using QcWorldgenParser::parseBiome;
using QcWorldgenParser::parseFeature;
using QcWorldgenParser::parseOre;
using QcWorldgenParser::parseTerrainSettings;

QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace);
QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json);
QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace);

} // namespace QcContentParser
