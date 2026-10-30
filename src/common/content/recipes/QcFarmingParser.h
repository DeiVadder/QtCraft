// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

struct QcJsonFieldReader;

/*! \brief Parses the farming fields of blocks (decisions E-289 and E-290): "crop" (stages, pace,
    light, fruit of stems) and "soil" (moisture, water reach, what it reverts to). */
namespace QcFarmingParser {

/*! Returns the "crop" object read by \a cropReader, block names qualified with
    \a packNamespace. \a block (read so far) must have the stage property with at least two
    values, need support from below and not be solid. Failures go to \a cropReader. */
[[nodiscard]] QcCropDefinition parseCrop(const QcBlockDefinition &block, QcJsonFieldReader &cropReader, const QString &packNamespace);
/*! Returns the "soil" object read by \a soilReader, block names qualified with
    \a packNamespace. \a block (read so far) must have the moisture property with at least two
    values, and \c {revertsTo} must name another block. Failures go to \a soilReader. */
[[nodiscard]] QcSoilDefinition parseSoil(const QcBlockDefinition &block, QcJsonFieldReader &soilReader, const QString &packNamespace);

} // namespace QcFarmingParser
