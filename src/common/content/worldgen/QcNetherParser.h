// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcNetherDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses worldgen/nether.json (decision E-1030). */
namespace QcNetherParser {

/*! Returns the nether settings read from \a json; names without a namespace belong to
    \a packNamespace. An error for values out of range or entries without their block. */
[[nodiscard]] QcResult<QcNetherSettings> parse(const QJsonObject &json, const QString &packNamespace);

} // namespace QcNetherParser
