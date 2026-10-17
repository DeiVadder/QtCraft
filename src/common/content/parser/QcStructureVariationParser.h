#pragma once

#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QHash>
#include <QJsonObject>

// Parses the variation of a structure (decision E-404): `choices` (named lists of name swaps, one
// option taken per placed copy) and `parts` (optional cells with a chance, in the structure's
// palette). Used by QcStructureParser after the palette and layers.
namespace QcStructureVariationParser {

// `keys` maps the palette characters to palette indices of `structure`.
void parseVariation(const QJsonObject &json, const QHash<QChar, qint16> &keys, QcStructureDefinition &structure, QcJsonFieldReader &reader,
                    const QString &packNamespace);

} // namespace QcStructureVariationParser
