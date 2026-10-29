// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses structures (structures/*.json, decision E-219).
namespace QcStructureParser {

[[nodiscard]] QcResult<QcStructureDefinition> parseStructure(const QJsonObject &json, const QString &packNamespace);

// "torch[attach=north]" with the block name qualified ("qtcraft:torch[attach=north]"); empty for
// invalid names.
QString qualifiedStateName(const QString &text, const QString &packNamespace);

} // namespace QcStructureParser
