#pragma once

#include "QcRecipeDefinition.h"
#include "QcResult.h"
#include "QcSmeltingRecipe.h"

#include <QJsonObject>

// Parses crafting and smelting recipes of content packs.
namespace QcRecipeParser {

QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace);
QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace);

} // namespace QcRecipeParser
