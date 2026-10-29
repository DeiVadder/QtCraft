// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

struct QcJsonFieldReader;

// Parses the farming fields of blocks (decisions E-289 and E-290): "crop" (stages, pace, light,
// fruit of stems) and "soil" (moisture, water reach, what it reverts to).
namespace QcFarmingParser {

// The crop and soil fields; `block` (read so far) must have the state properties they name.
[[nodiscard]] QcCropDefinition parseCrop(const QcBlockDefinition &block, QcJsonFieldReader &cropReader, const QString &packNamespace);
[[nodiscard]] QcSoilDefinition parseSoil(const QcBlockDefinition &block, QcJsonFieldReader &soilReader, const QString &packNamespace);

} // namespace QcFarmingParser
