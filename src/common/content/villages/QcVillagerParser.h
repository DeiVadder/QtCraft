// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/villages/QcVillagerDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses the settings of a villager species (villagers/<name>.json), the professions
    (professions/<name>.json) and the look of a village style's villagers (decision E-411, E-421).

    All input is untrusted. */
namespace QcVillagerParser {

/*! Returns the villager species defined by \a json (a file in \c {villagers}), names
    qualified with \a packNamespace, or the first error. */
[[nodiscard]] QcResult<QcVillagerSettings> parseVillagerSettings(const QJsonObject &json, const QString &packNamespace);
/*! Returns the profession defined by \a json (a file in \c {professions}), names qualified
    with \a packNamespace, or the first error. */
[[nodiscard]] QcResult<QcVillagerProfession> parseProfession(const QJsonObject &json, const QString &packNamespace);
/*! Returns the look at \a key of the object of \a reader (outfit, hair and colour lists), names
    qualified with \a packNamespace; unset without the key. */
QcVillagerLook readLook(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader);

} // namespace QcVillagerParser
