#pragma once

#include "QcCommandMacroDefinition.h"
#include "QcResult.h"

#include <QJsonObject>

// Parses command macros (commands/*.json, decision E-248).
namespace QcCommandMacroParser {

[[nodiscard]] QcResult<QcCommandMacroDefinition> parseCommandMacro(const QJsonObject &json, const QString &packNamespace);

} // namespace QcCommandMacroParser
