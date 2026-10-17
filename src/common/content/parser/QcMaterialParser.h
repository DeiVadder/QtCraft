#pragma once

#include "common/content/definitions/QcMaterialDefinition.h"
#include "common/content/definitions/QcPartFamily.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses materials and the part families that turn them into building parts.
namespace QcMaterialParser {

[[nodiscard]] QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace);
// The recipe file of a part: the family's recipe with "@" replaced by the base block.
QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);

} // namespace QcMaterialParser
