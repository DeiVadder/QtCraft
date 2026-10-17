#pragma once

#include "common/content/definitions/QcRecipeDefinition.h"
#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses crafting and smelting recipes of content packs.
namespace QcRecipeParser {

[[nodiscard]] QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace);

} // namespace QcRecipeParser
