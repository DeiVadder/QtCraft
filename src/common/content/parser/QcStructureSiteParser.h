#pragma once

#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonObject>

// Parses where the world generator places a structure by itself and what guards it (decisions
// E-590, E-591): `groundLayer`, `surface` (biomes, region spacing, chance, attempts, slope, margin
// to villages, foundation) and `guardians` (zones whose creatures come back). Used by
// QcStructureParser after the layers, so the zones are checked against the structure's size.
namespace QcStructureSiteParser {

void parseSite(const QJsonObject &json, QcStructureDefinition &structure, QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcStructureSiteParser
