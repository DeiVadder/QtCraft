#pragma once

#include "QcBlockDefinition.h"
#include "QcResult.h"

#include <QJsonObject>

// Parses block definitions of content packs.
namespace QcBlockParser {

[[nodiscard]] QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace);

} // namespace QcBlockParser
