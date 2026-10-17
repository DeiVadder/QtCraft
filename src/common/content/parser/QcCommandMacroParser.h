#pragma once

#include "common/content/definitions/QcCommandMacroDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses command macros (commands/*.json, decision E-248).
namespace QcCommandMacroParser {

[[nodiscard]] QcResult<QcCommandMacroDefinition> parseCommandMacro(const QJsonObject &json, const QString &packNamespace);

} // namespace QcCommandMacroParser
