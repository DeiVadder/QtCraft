// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcEnchantmentDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses an enchantment (enchantments/<name>.json, decision E-864). All input is untrusted.
namespace QcEnchantmentParser {

[[nodiscard]] QcResult<QcEnchantmentDefinition> parseEnchantment(const QJsonObject &json, const QString &packNamespace);

// The effect named `text` ("efficiency", "unbreaking", …); none for an unknown name.
[[nodiscard]] std::optional<QcEnchantmentEffect> effectOf(const QString &text);

// The item kinds an enchantment may name in `items` (QcEnchanting::kindsOf).
[[nodiscard]] bool isItemKind(const QString &kind);

} // namespace QcEnchantmentParser
