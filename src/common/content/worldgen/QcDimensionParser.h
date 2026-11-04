// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses a dimension (dimensions/<name>.json, decision E-978).

    All input is untrusted. */
namespace QcDimensionParser {

/*! Returns the dimension read from \a json, its names qualified with \a packNamespace, or the
    first failure. */
[[nodiscard]] QcResult<QcDimensionDefinition> parseDimension(const QJsonObject &json, const QString &packNamespace);

} // namespace QcDimensionParser
