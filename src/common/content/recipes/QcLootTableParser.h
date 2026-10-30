// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/recipes/QcLootTableDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses a loot table (loot/<name>.json, decision E-521).

    All input is untrusted; item names are checked when the table is rolled (an unknown item
    gives nothing). */
namespace QcLootTableParser {

/*! Returns the loot table read from \a json, its names qualified with \a packNamespace, or the
    first failure. */
[[nodiscard]] QcResult<QcLootTableDefinition> parseLootTable(const QJsonObject &json, const QString &packNamespace);

} // namespace QcLootTableParser
