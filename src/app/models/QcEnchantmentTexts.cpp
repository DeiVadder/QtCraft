// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcEnchantmentTexts.h"

#include "common/content/registry/QcContentRegistry.h"

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace QcEnchantmentTexts {

QString roman(int level)
{
    static constexpr std::array<QLatin1StringView, 10> numerals{
        QLatin1StringView("I"),  QLatin1StringView("II"),  QLatin1StringView("III"), QLatin1StringView("IV"), QLatin1StringView("V"),
        QLatin1StringView("VI"), QLatin1StringView("VII"), QLatin1StringView("VIII"), QLatin1StringView("IX"), QLatin1StringView("X"),
    };
    return level >= 1 && std::cmp_less_equal(level, numerals.size()) ? QString(numerals[static_cast<std::size_t>(level - 1)]) : QString::number(level);
}

QString nameOf(const QcContentRegistry &content, const QcEnchantment &enchantment, const QString &language)
{
    const QcEnchantmentDefinition *definition = content.enchantment(enchantment.name);
    if (!definition) {
        return enchantment.name;
    }
    const QString name = content.translations().translate(definition->translationKey, language);
    return definition->maxLevel <= 1 && enchantment.level <= 1 ? name : name + u' ' + roman(enchantment.level);
}

QStringList linesOf(const QcContentRegistry &content, const QcItemStack &stack, const QString &language)
{
    QStringList lines;
    for (const QcEnchantment &enchantment : stack.enchantments()) {
        lines.append(nameOf(content, enchantment, language));
    }
    return lines;
}

} // namespace QcEnchantmentTexts
