// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcCoastalTowerDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses the coastal towers of the terrain (`"coastalTowers"` in worldgen/terrain.json, decision
// E-605); the structures and the foundation block are checked by the generator.
namespace QcCoastalTowerParser {

[[nodiscard]] QcCoastalTowerSettings parseCoastalTowers(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcCoastalTowerParser
