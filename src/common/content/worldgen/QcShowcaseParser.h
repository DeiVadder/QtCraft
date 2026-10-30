// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcShowcaseDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses the showcase settings (worldgen/showcase.json, decision E-220). */
namespace QcShowcaseParser {

/*! Returns the showcase settings read from \a json, or the first error. Block and structure
    names are qualified with \a packNamespace. */
[[nodiscard]] QcResult<QcShowcaseDefinition> parseShowcase(const QJsonObject &json, const QString &packNamespace);

} // namespace QcShowcaseParser
