// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcMaterialDefinition.h"
#include "common/content/blocks/QcPartFamily.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses materials and the part families that turn them into building parts.
namespace QcMaterialParser {

[[nodiscard]] QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace);
// The recipe file of a part: the family's recipe with "@" replaced by the base block and
// "@<suffix>" by the material's part of that suffix.
QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);
// The recipe files of the family's further recipes (moreRecipes) for one part, resolved the same way.
QList<QJsonObject> morePartRecipesJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);
// The suffixes of other parts of the same material the family's recipes name ("@slab"); a part is
// only made for materials that have them.
[[nodiscard]] QStringList siblingSuffixes(const QcPartFamily &family);

} // namespace QcMaterialParser
