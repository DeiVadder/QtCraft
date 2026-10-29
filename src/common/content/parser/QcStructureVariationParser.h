// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QHash>
#include <QJsonObject>

// Parses the variation of a structure (decision E-404): `choices` (named lists of name swaps, one
// option taken per placed copy) and `parts` (optional cells with a chance, in the structure's
// palette). Used by QcStructureParser after the palette and layers, choices first.
namespace QcStructureVariationParser {

// The named lists of name swaps at "choices".
[[nodiscard]] QList<QcStructureDefinition::Choice> parseChoices(const QJsonObject &json, QcJsonFieldReader &reader,
                                                                const QString &packNamespace);
// The optional parts at "parts"; `keys` maps the palette characters to palette indices of `structure`
// (read so far: its palette, size and cells).
[[nodiscard]] QList<QcStructureDefinition::Part> parseParts(const QJsonObject &json, const QHash<QChar, qint16> &keys,
                                                            const QcStructureDefinition &structure, QcJsonFieldReader &reader);

} // namespace QcStructureVariationParser
