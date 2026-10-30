// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcWorldOptionDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses worldgen/options.json of a content pack (decision E-917): the groups, options and
    presets of the world generation.

    Whether a target exists and suits the range is checked later, against the whole content
    (QcWorldOptions::check), since a pack may set options on another pack's content. */
namespace QcWorldOptionParser {

/*! At most this many options, groups and presets per pack, and values per preset. */
inline constexpr qsizetype maxEntries = 128;

/*! Returns the groups, options and presets read from \a json, or the first error. Names are
    qualified with \a packNamespace. */
[[nodiscard]] QcResult<QcWorldOptionSet> parseOptionSet(const QJsonObject &json, const QString &packNamespace);

} // namespace QcWorldOptionParser
