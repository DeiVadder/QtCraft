#pragma once

#include "QcResult.h"
#include "QcShapeDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses block shapes: shape files and the inline shapes of blocks.
namespace QcShapeParser {

[[nodiscard]] QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace);
// Shape body shared by shape files and inline block shapes: "parts", or "boxes"/"crosses"/"collision"
// directly for a shape of one unconditional part.
void parseShapeBody(const QJsonObject &json, QcShapeDefinition &shape, QcJsonFieldReader &reader);
// The conditions at "when" ({"property": value or [values]}), as shape parts and block drops use them.
QList<QcShapeCondition> parseConditions(const QJsonObject &json, QcJsonFieldReader &reader);

} // namespace QcShapeParser
