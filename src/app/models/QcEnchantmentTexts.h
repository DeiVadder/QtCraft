#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QString>
#include <QStringList>

class QcContentRegistry;

// How the client names enchantments (decision E-864): the translated name and the level as a Roman
// numeral ("Efficiency III"), for tooltips and the enchanting table's clues.
namespace QcEnchantmentTexts {

// "Efficiency III"; the level is left out for an enchantment with one level. An enchantment the
// content does not know keeps its name.
[[nodiscard]] QString nameOf(const QcContentRegistry &content, const QcEnchantment &enchantment, const QString &language);
// One line per enchantment of `stack`, in its order.
[[nodiscard]] QStringList linesOf(const QcContentRegistry &content, const QcItemStack &stack, const QString &language);
// 1 → "I" … 10 → "X"; larger levels as digits.
[[nodiscard]] QString roman(int level);

} // namespace QcEnchantmentTexts
