// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses structures (structures/\unicode {0x2A}.json, decision E-219). */
namespace QcStructureParser {

/*! Returns the structure read from \a json, or the first error. Names are qualified with
    \a packNamespace. */
[[nodiscard]] QcResult<QcStructureDefinition> parseStructure(const QJsonObject &json, const QString &packNamespace);

/*! Returns the block state \a text ("torch[attach=north]") with its block name qualified with
    \a packNamespace ("qtcraft:torch[attach=north]"); empty for an invalid name or a missing
    closing bracket. */
QString qualifiedStateName(const QString &text, const QString &packNamespace);

} // namespace QcStructureParser
