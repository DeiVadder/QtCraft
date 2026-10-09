#pragma once

#include "QcAppearanceDefinition.h"
#include "QcBlockDefinition.h"
#include "QcBlockStateTable.h"
#include "QcHandbookChapter.h"
#include "QcItemDefinition.h"
#include "QcMaterialDefinition.h"
#include "QcPartFamily.h"
#include "QcMobDefinition.h"
#include "QcModelDefinition.h"
#include "QcRecipeDefinition.h"
#include "QcResult.h"
#include "QcSmeltingRecipe.h"
#include "QcSoundDefinition.h"
#include "QcTextureDefinition.h"
#include "QcWorldgenDefinitions.h"

#include <QHash>
#include <QJsonObject>

// Turns content-pack JSON documents into definitions. Unqualified names get the pack's namespace.
// All input is untrusted: every field is type-checked and range-checked.
namespace QcContentParser {

QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace);
QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace);
QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace);
QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace);
// The recipe file of a part: the family's recipe with "@" replaced by the base block.
QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);
QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace);
QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json);
QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace);
QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace);
QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace);
QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace);
QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace);
QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace);
QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace);
QcResult<QcBiomeDefinition> parseBiome(const QJsonObject &json, const QString &packNamespace);
QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace);
QcResult<QcOreDefinition> parseOre(const QJsonObject &json, const QString &packNamespace);
QcResult<QcTerrainSettings> parseTerrainSettings(const QJsonObject &json, const QString &packNamespace);
QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace);

} // namespace QcContentParser
