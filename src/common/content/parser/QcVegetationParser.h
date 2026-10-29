// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcVegetationDefinition.h"

#include <QString>

struct QcJsonFieldReader;

// Parses the fields of the `vegetation` object of a biome (decision E-814): `grass` and `flowers`, each with
// `density`, `patchSize`, `plants` (block names or objects with `block` and `weight`) and
// `patches` (`chance`, `tries`, `radius`). Problems are reported through `reader`; the blocks are
// checked when the tables are resolved (QcVegetationTable).
namespace QcVegetationParser {

[[nodiscard]] QcVegetationDefinition parse(QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcVegetationParser
