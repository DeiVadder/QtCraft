#pragma once

#include "QcResult.h"
#include "QcShapeDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses block shapes: shape files and the inline shapes of blocks.
namespace QcShapeParser {

QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace);
// Shape body shared by shape files and inline block shapes: "parts", or "boxes"/"crosses"/"collision"
// directly for a shape of one unconditional part.
void parseShapeBody(const QJsonObject &json, QcShapeDefinition &shape, QcJsonFieldReader &reader);

} // namespace QcShapeParser
