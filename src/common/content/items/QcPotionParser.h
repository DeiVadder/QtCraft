// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcPotionDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

struct QcJsonFieldReader;

/*! \brief Parses kinds of potions (potions/<name>.json) and brewing recipes (brewing/<name>.json,
    decisions E-1061, E-1062). */
namespace QcPotionParser {

/*! Returns the effects listed in \c {"effects"} of \a json (a potion or a drink), their names
    qualified with \a packNamespace; a wrong entry fails \a reader. */
[[nodiscard]] QList<QcPotionEffect> parseEffects(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);
/*! Returns the potion read from \a json, its names qualified with \a packNamespace, or the first
    error. */
[[nodiscard]] QcResult<QcPotionDefinition> parsePotion(const QJsonObject &json, const QString &packNamespace);
/*! Returns the brewing recipe read from \a json, its names qualified with \a packNamespace, or
    the first error. */
[[nodiscard]] QcResult<QcBrewingRecipe> parseBrewingRecipe(const QJsonObject &json, const QString &packNamespace);
/*! Returns the data name of \a form. */
[[nodiscard]] QString formName(QcPotionForm form);

} // namespace QcPotionParser
