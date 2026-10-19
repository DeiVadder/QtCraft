#pragma once

#include "common/content/definitions/QcCoastalTowerDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses the coastal towers of the terrain (`"coastalTowers"` in worldgen/terrain.json, decision
// E-605); the structures and the foundation block are checked by the generator.
namespace QcCoastalTowerParser {

void parseCoastalTowers(const QJsonObject &json, QcCoastalTowerSettings &towers, QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcCoastalTowerParser
