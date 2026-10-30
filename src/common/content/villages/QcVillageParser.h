// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/villages/QcVillageDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses village styles (villages/\unicode {0x2A}.json) and the village settings
    (worldgen/villages.json), decision E-403.

    A style with "extends" (decision E-421) starts from the style it names: without its own
    "name" it patches that style, with one it is a new style (without the base's biomes and
    fallback unless it gives them). It replaces the base's fields it gives, takes the biomes of
    its "remove" object and appends the lists of its "add" object (biomes, crops, pieces,
    layouts). */
namespace QcVillageParser {

/*! Returns the village style defined by \a json, names qualified with \a packNamespace, or the
    first error; a file with "extends" is an error here (extendVillageStyle() reads it). */
[[nodiscard]] QcResult<QcVillageStyleDefinition> parseVillageStyle(const QJsonObject &json, const QString &packNamespace);
/*! Returns the style that \a json extends, qualified with \a packNamespace; an error when
    "extends" is no valid name. */
[[nodiscard]] QcResult<QString> extendedStyle(const QJsonObject &json, const QString &packNamespace);
/*! Returns the style \a json makes of \a base, names qualified with \a packNamespace, or the
    first error: \a base patched when \a json has no other "name", else a new style (see
    above). */
[[nodiscard]] QcResult<QcVillageStyleDefinition> extendVillageStyle(const QJsonObject &json, const QString &packNamespace,
                                                                    const QcVillageStyleDefinition &base);
/*! Returns the village settings defined by \a json over the defaults, names qualified with
    \a packNamespace, or the first error. */
[[nodiscard]] QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace);
/*! Returns the settings of a later pack (\a json, names qualified with \a packNamespace) read
    over \a settings (the earlier packs'): the fields it gives replace theirs, \c {golem} field
    by field (decision E-421). */
[[nodiscard]] QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace,
                                                               QcVillageSettings settings);

} // namespace QcVillageParser
