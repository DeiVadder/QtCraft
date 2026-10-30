// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcEnchantmentDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses an enchantment (enchantments/<name>.json, decision E-864).

    All input is untrusted. */
namespace QcEnchantmentParser {

/*! Returns the enchantment read from \a json, its names qualified with \a packNamespace, or the
    first failure. */
[[nodiscard]] QcResult<QcEnchantmentDefinition> parseEnchantment(const QJsonObject &json, const QString &packNamespace);

/*! Returns the effect named \a text ("efficiency", "unbreaking", …); none for an unknown name. */
[[nodiscard]] std::optional<QcEnchantmentEffect> effectOf(const QString &text);

/*! Returns whether \a kind is one of the item kinds an enchantment may name in \c {items}
    (QcEnchanting::kindsOf). */
[[nodiscard]] bool isItemKind(const QString &kind);

} // namespace QcEnchantmentParser
