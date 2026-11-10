// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStrongholdDefinition.h"

struct QcJsonFieldReader;

/*! \brief Parses the strongholds of the terrain (\c {"strongholds"} in worldgen/terrain.json,
    decision E-1170); the blocks are checked by the generator. */
namespace QcStrongholdParser {

/*! Returns the stronghold settings read through \a reader, the reader of the
    \c {"strongholds"} object. Names are qualified with \a packNamespace. A missing block, mob or
    loot table fails \a reader. */
[[nodiscard]] QcStrongholdSettings parseStrongholds(QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcStrongholdParser
