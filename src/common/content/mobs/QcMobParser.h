// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearanceDefinition.h"
#include "common/content/mobs/QcMobDefinition.h"
#include "common/content/mobs/QcModelDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses mobs and the skeleton models and appearances that characters are drawn with. */
namespace QcMobParser {

/*! Returns the creature defined by \a json (a file in \c {mobs}), names qualified with
    \a packNamespace, or the first error (QcMobReader). */
[[nodiscard]] QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace);
/*! Returns the skeleton model defined by \a json (a file in \c {models}), names qualified with
    \a packNamespace, or the first error. */
[[nodiscard]] QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace);
/*! Returns the outfit, hair style or cape defined by \a json (a file in \c {appearance}), names
    qualified with \a packNamespace, or the first error. */
[[nodiscard]] QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace);

} // namespace QcMobParser
