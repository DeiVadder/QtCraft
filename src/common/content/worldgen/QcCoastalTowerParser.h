// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcCoastalTowerDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

/*! \brief Parses the coastal towers of the terrain (\c {"coastalTowers"} in
    worldgen/terrain.json, decision E-605); the structures and the foundation block are checked
    by the generator. */
namespace QcCoastalTowerParser {

/*! Returns the coastal tower settings read from \a json, the \c {"coastalTowers"} object,
    through \a reader. Block and structure names are qualified with \a packNamespace. A missing
    \c {"foundation"} or empty \c {"structures"} fails \a reader. */
[[nodiscard]] QcCoastalTowerSettings parseCoastalTowers(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcCoastalTowerParser
