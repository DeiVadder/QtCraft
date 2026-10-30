// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/recipes/QcRecipeDefinition.h"
#include "common/content/recipes/QcSmeltingRecipe.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses crafting and smelting recipes of content packs. */
namespace QcRecipeParser {

/*! Returns the crafting recipe read from \a json, its names qualified with \a packNamespace, or
    the first failure. The pattern loses blank rows and columns at its edges. */
[[nodiscard]] QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace);
/*! Returns the smelting recipe read from \a json, its names qualified with \a packNamespace, or
    the first failure; the category "stonecutting" is refused. */
[[nodiscard]] QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace);
/*! Returns the stonecutting recipe (decision E-784) read from \a json, its names qualified with
    \a packNamespace, or the first failure: the fields of a smelting recipe, always of the
    category "stonecutting" (a \c {"category"} in the file is ignored); seconds and experience
    mean nothing to it. A name longer than QcStonecutting::maxRecipeName is refused. */
[[nodiscard]] QcResult<QcSmeltingRecipe> parseStonecuttingRecipe(const QJsonObject &json, const QString &packNamespace);

} // namespace QcRecipeParser
