#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses block definitions of content packs.
namespace QcBlockParser {

[[nodiscard]] QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace);

} // namespace QcBlockParser
