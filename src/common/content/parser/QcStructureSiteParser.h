#pragma once

#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonObject>

#include <optional>

// Parses where the world generator places a structure by itself and what guards it (decisions
// E-590, E-591): `groundLayer`, `surface` (biomes, region spacing, chance, attempts, slope, margin
// to villages, foundation) and `guardians` (zones whose creatures come back). Used by
// QcStructureParser after the layers, so the zones are checked against the structure's size.
namespace QcStructureSiteParser {

// Where and how a structure is placed by the generator: the fields of QcStructureDefinition of the
// same names.
struct Site
{
    int groundLayer = 0;
    std::optional<QcStructureDefinition::Surface> surface;
    QList<QcStructureDefinition::Guardian> guardians;
};

// `structure` is the structure read so far (its size bounds the zones and the ground layer).
[[nodiscard]] Site parseSite(const QJsonObject &json, const QcStructureDefinition &structure, QcJsonFieldReader &reader,
                             const QString &packNamespace);

} // namespace QcStructureSiteParser
