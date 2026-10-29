// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcRecipeDefinition.h"
#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses crafting and smelting recipes of content packs.
namespace QcRecipeParser {

[[nodiscard]] QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace);
// A stonecutting recipe (decision E-784): the fields of a smelting recipe, always of the category
// "stonecutting"; seconds and experience mean nothing to it.
[[nodiscard]] QcResult<QcSmeltingRecipe> parseStonecuttingRecipe(const QJsonObject &json, const QString &packNamespace);

} // namespace QcRecipeParser
