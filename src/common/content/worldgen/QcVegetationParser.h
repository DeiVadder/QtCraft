// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcVegetationDefinition.h"

#include <QString>

struct QcJsonFieldReader;

/*! \brief Parses the fields of the \c {vegetation} object of a biome (decision E-814): \c {grass}
    and \c {flowers}, each with \c {density}, \c {patchSize}, \c {plants} (block names or objects
    with \c {block} and \c {weight}) and \c {patches} (\c {chance}, \c {tries}, \c {radius}).

    Problems are reported through \c {reader}; the blocks are checked when the tables are
    resolved (QcVegetationTable). */
namespace QcVegetationParser {

/*! Returns the grass and flower layers read through \a reader, the reader of the biome's
    \c {vegetation} object; a missing layer stays empty. Plant names are qualified with
    \a packNamespace. */
[[nodiscard]] QcVegetationDefinition parse(QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcVegetationParser
