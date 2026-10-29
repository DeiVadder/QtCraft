// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses the "flow" object of a liquid block: how far and how fast it spreads,
// whether it makes new sources, its bucket, bottle and sound, and its reactions with other liquids.
namespace QcLiquidParser {

// The liquid of the "flow" object `flowJson`; problems go to `flowReader`.
[[nodiscard]] QcLiquidDefinition parseFlow(const QJsonObject &flowJson, QcJsonFieldReader &flowReader, const QString &packNamespace);
// The flow keeps its state in `level` ("0" source, then one value per block of distance) and
// optionally `falling`; `block` (with its flow) must have them.
void checkStates(const QcBlockDefinition &block, QcJsonFieldReader &flowReader);

} // namespace QcLiquidParser
