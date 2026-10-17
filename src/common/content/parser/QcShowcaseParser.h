#pragma once

#include "common/content/definitions/QcShowcaseDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses the showcase settings (worldgen/showcase.json, decision E-220).
namespace QcShowcaseParser {

[[nodiscard]] QcResult<QcShowcaseDefinition> parseShowcase(const QJsonObject &json, const QString &packNamespace);

} // namespace QcShowcaseParser
