#pragma once

#include "common/content/definitions/QcLootTableDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses a loot table (loot/<name>.json, decision E-521). All input is untrusted; item names are
// checked when the table is rolled (an unknown item gives nothing).
namespace QcLootTableParser {

[[nodiscard]] QcResult<QcLootTableDefinition> parseLootTable(const QJsonObject &json, const QString &packNamespace);

} // namespace QcLootTableParser
