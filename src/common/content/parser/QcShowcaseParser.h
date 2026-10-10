#pragma once

#include "QcResult.h"
#include "QcShowcaseDefinition.h"

#include <QJsonObject>

// Parses the showcase settings (worldgen/showcase.json, decision E-220).
namespace QcShowcaseParser {

QcResult<QcShowcaseDefinition> parseShowcase(const QJsonObject &json, const QString &packNamespace);

} // namespace QcShowcaseParser
