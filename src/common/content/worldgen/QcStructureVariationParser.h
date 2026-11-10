// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/worldgen/QcStructureDefinition.h"

#include <QHash>
#include <QJsonObject>

/*! \brief Parses the variation of a structure (decision E-404): \c {choices} (named lists of name
    swaps, one option taken per placed copy) and \c {parts} (optional cells with a chance, in the
    structure's palette).

    Used by QcStructureParser after the palette and layers, choices first. */
namespace QcStructureVariationParser {

/*! Returns the named lists of name swaps at "choices" of \a json, names qualified with
    \a packNamespace; \a reader fails on an invalid entry. */
[[nodiscard]] QList<QcStructureDefinition::Choice> parseChoices(const QJsonObject &json, QcJsonFieldReader &reader,
                                                                const QString &packNamespace);
/*! Returns the loot chest of the entry \a entry of "loot" (\c {at}, a cell of \a structure, and
    \c {table}, qualified with \a packNamespace); std::nullopt when it is invalid. */
[[nodiscard]] std::optional<QcStructureDefinition::Loot> readLoot(const QJsonObject &entry, const QcStructureDefinition &structure,
                                                                  const QString &packNamespace);
/*! Returns the changes of generator versions at "generatorChanges" of \a json (decision E-1016) for
    \a structure, whose size is known, names qualified with \a packNamespace; \a reader fails on an
    invalid entry. */
[[nodiscard]] QList<QcStructureDefinition::VersionChange> parseGeneratorChanges(const QJsonObject &json, const QcStructureDefinition &structure,
                                                                                QcJsonFieldReader &reader, const QString &packNamespace);
/*! Returns the optional parts at "parts" of \a json; \a reader fails on an invalid entry.
    \a keys maps the palette characters to palette indices
    of \a structure (read so far: its palette, size and cells). */
[[nodiscard]] QList<QcStructureDefinition::Part> parseParts(const QJsonObject &json, const QHash<QChar, qint16> &keys,
                                                            const QcStructureDefinition &structure, QcJsonFieldReader &reader);

} // namespace QcStructureVariationParser
