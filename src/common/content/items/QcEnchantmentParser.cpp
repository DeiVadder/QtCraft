// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcEnchantmentParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/gameplay/QcItemStack.h"

#include <QJsonArray>

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The item kinds of `items`: at most 16 known kind names; else the reader fails.
QStringList parseItemKinds(QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"items"_s);
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.isEmpty() || list.size() > 16) {
        reader.fail(u"'items' must be an array of 1 to 16 item kinds"_s);
        return {};
    }

    QStringList kinds;
    for (const auto &entry : list) {
        const QString kind = entry.toString();
        if (!QcEnchantmentParser::isItemKind(kind)) {
            reader.fail(u"unknown item kind '%1' in 'items'"_s.arg(kind));
            return {};
        }
        kinds.append(kind);
    }
    return kinds;
}

} // namespace

namespace QcEnchantmentParser {

std::optional<QcEnchantmentEffect> effectOf(const QString &text)
{
    static constexpr std::array<std::pair<QLatin1StringView, QcEnchantmentEffect>, 5> effects{{
        {QLatin1StringView("efficiency"), QcEnchantmentEffect::Efficiency},
        {QLatin1StringView("unbreaking"), QcEnchantmentEffect::Unbreaking},
        {QLatin1StringView("sharpness"), QcEnchantmentEffect::Sharpness},
        {QLatin1StringView("protection"), QcEnchantmentEffect::Protection},
        {QLatin1StringView("feather_falling"), QcEnchantmentEffect::FeatherFalling},
    }};
    for (const auto &[name, effect] : effects) {
        if (text == name) {
            return effect;
        }
    }
    return std::nullopt;
}

bool isItemKind(const QString &kind)
{
    static constexpr std::array<QLatin1StringView, 14> kinds{
        QLatin1StringView("pickaxe"),     QLatin1StringView("axe"),         QLatin1StringView("shovel"),
        QLatin1StringView("hoe"),         QLatin1StringView("shears"),      QLatin1StringView("digger"),
        QLatin1StringView("weapon"),      QLatin1StringView("armour"),      QLatin1StringView("armour_head"),
        QLatin1StringView("armour_chest"), QLatin1StringView("armour_legs"), QLatin1StringView("armour_feet"),
        QLatin1StringView("launcher"),    QLatin1StringView("durable"),
    };
    return std::ranges::find(kinds, kind) != kinds.end();
}

QcResult<QcEnchantmentDefinition> parseEnchantment(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"enchantment"_s, {}};
    QcEnchantmentDefinition enchantment;
    enchantment.name = reader.name(u"name"_s, packNamespace);
    if (enchantment.name.isEmpty()) {
        return qcError(reader.ok() ? u"enchantment: 'name' is required"_s : reader.error);
    }
    // The name travels in item data, whose readers refuse longer names (QcItemData::fitsLimits).
    if (enchantment.name.size() > QcItemData::maxEnchantmentName) {
        return qcError(u"enchantment '%1': the name with its namespace is longer than %2 characters"_s.arg(enchantment.name).arg(QcItemData::maxEnchantmentName));
    }
    reader.context = u"enchantment '%1'"_s.arg(enchantment.name);

    enchantment.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"enchantment"_s, enchantment.name));
    const QString effect = reader.string(u"effect"_s);
    if (const std::optional<QcEnchantmentEffect> known = effectOf(effect)) {
        enchantment.effect = *known;
    } else {
        reader.fail(u"unknown 'effect' '%1'"_s.arg(effect));
    }
    enchantment.maxLevel = reader.integer(u"maxLevel"_s, 1, 1, QcEnchantmentDefinition::maxLevelLimit);
    enchantment.weight = reader.integer(u"weight"_s, enchantment.weight, 1, 1000);
    enchantment.items = parseItemKinds(reader);
    enchantment.conflicts = QcContentFields::parseNames(u"conflicts"_s, packNamespace, reader);
    enchantment.minPower = reader.integer(u"minPower"_s, enchantment.minPower, 1, 100);
    enchantment.powerPerLevel = reader.integer(u"powerPerLevel"_s, enchantment.powerPerLevel, 0, 100);
    enchantment.powerSpread = reader.integer(u"powerSpread"_s, enchantment.powerSpread, 0, 100);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return enchantment;
}

} // namespace QcEnchantmentParser
