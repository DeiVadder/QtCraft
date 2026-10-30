// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcMaterialDefinition.h"
#include "common/content/blocks/QcPartFamily.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses materials and the part families that turn them into building parts. */
namespace QcMaterialParser {

/*! Returns the part family read from \a json, names qualified in \a packNamespace, or the error;
    its block and recipe templates are checked on a sample material. */
[[nodiscard]] QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace);
/*! Returns the material or exclusion entry read from \a json, names qualified in
    \a packNamespace, or the error. */
[[nodiscard]] QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace);
/*! Returns the recipe file of the part \a partName: the recipe of \a family with "@" replaced by
    \a baseBlock and "@<suffix>" by the material's part of that suffix; the recipe is named
    \a partName and makes it unless the template names a "result". */
QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);
/*! Returns the recipe files of the further recipes (moreRecipes) of \a family for the part
    \a partName made from \a baseBlock, resolved the same way; each is named
    "<partName>_<name>". */
QList<QJsonObject> morePartRecipesJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);
/*! Returns the suffixes of other parts of the same material that the recipes of \a family name
    ("@slab"); a part is only made for materials that have them. */
[[nodiscard]] QStringList siblingSuffixes(const QcPartFamily &family);

} // namespace QcMaterialParser
