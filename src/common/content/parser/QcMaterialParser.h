#pragma once

#include "QcMaterialDefinition.h"
#include "QcPartFamily.h"
#include "QcResult.h"

#include <QJsonObject>

// Parses materials and the part families that turn them into building parts.
namespace QcMaterialParser {

QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace);
QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace);
// The recipe file of a part: the family's recipe with "@" replaced by the base block.
QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName);

} // namespace QcMaterialParser
