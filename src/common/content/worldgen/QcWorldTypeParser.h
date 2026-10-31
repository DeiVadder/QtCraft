// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcWorldTypeDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

class QcContentRegistry;

/*! \brief Parses the settings of the flat and void world types (worldgen/flat.json,
    worldgen/void.json, decision E-223). */
namespace QcWorldTypeParser {

/*! Returns the flat world settings read from \a json, or the first error. Block names of the
    preset without a namespace are qualified with \a packNamespace. */
[[nodiscard]] QcResult<QcFlatDefinition> parseFlat(const QJsonObject &json, const QString &packNamespace);
/*! Returns the void world settings read from \a json, or the first error. The platform block is
    qualified with \a packNamespace. */
[[nodiscard]] QcResult<QcVoidDefinition> parseVoid(const QJsonObject &json, const QString &packNamespace);
/*! Returns the first block name of the layer list \a preset that \a content does not know, or an
    empty string when it knows them all (or the list is malformed: parsing reports that). */
[[nodiscard]] QString unknownPresetBlock(const QString &preset, const QcContentRegistry &content);
/*! Returns the layer list \a preset with the base game's namespace left out of its block names
    ("qtcraft:stone" becomes "stone"), as the menu shows it and a flat world stores it; the flat
    generator reads names without a namespace as the base game's. */
[[nodiscard]] QString shortPreset(const QString &preset);

} // namespace QcWorldTypeParser
