// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/villages/QcTradeParser.h"

#include "common/content/registry/QcContentFields.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int maxStackCount = 64;

// {"item": name, "count": n}; an empty item when it is broken (the reader holds the error).
QcTradeStack readStack(const QJsonValue &value, const QString &what, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcJsonFieldReader nested{value.toObject(), reader.context + u' ' + what, {}};
    QcTradeStack stack;
    stack.item = nested.name(u"item"_s, packNamespace);
    stack.count = nested.integer(u"count"_s, 1, 1, maxStackCount);
    if (!value.isObject() || stack.item.isEmpty()) {
        nested.fail(u"needs an 'item' and optionally a 'count' (1 to %1)"_s.arg(maxStackCount));
    }
    reader.adopt(nested);
    return stack;
}

QcTradeOffer readOffer(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcJsonFieldReader nested{value.toObject(), reader.context + u" offer"_s, {}};
    QcTradeOffer offer;
    offer.level = nested.integer(u"level"_s, 1, 1, QcTradingSettings::levelCount);

    const QJsonArray costs = value.toObject().value(u"costs"_s).toArray();
    if (!value.isObject() || costs.isEmpty() || costs.size() > QcTradeOffer::maxCosts) {
        nested.fail(u"needs 'costs' with 1 to %1 stacks"_s.arg(QcTradeOffer::maxCosts));
    }
    for (const auto &cost : costs) {
        offer.costs.append(readStack(cost, u"cost"_s, packNamespace, nested));
    }
    offer.result = readStack(value.toObject().value(u"result"_s), u"result"_s, packNamespace, nested);

    // The default id: the first cost and the result ("wheat>emerald").
    const QString fallbackId = offer.costs.isEmpty() ? QString() : offer.costs.constFirst().item + u'>' + offer.result.item;
    offer.id = nested.string(u"id"_s, fallbackId);
    offer.maxUses = nested.integer(u"maxUses"_s, offer.maxUses, 1, QcTradeOffer::maxUsesLimit);
    offer.villagerExperience = nested.integer(u"villagerExperience"_s, offer.villagerExperience, 0, QcTradeOffer::maxExperience);
    offer.playerExperience = nested.integer(u"playerExperience"_s, offer.playerExperience, 0, QcTradeOffer::maxExperience);
    offer.demand = nested.integer(u"demand"_s, offer.demand, 0, maxStackCount);
    if (nested.ok() && (offer.id.isEmpty() || offer.id.size() > 128)) {
        nested.fail(u"needs an 'id' of 1 to 128 characters"_s);
    }
    reader.adopt(nested);
    return offer;
}

} // namespace

namespace QcTradeParser {

QcResult<QcTradeSet> parseTradeSet(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"trades"_s, {}};
    QcTradeSet set;
    set.name = reader.name(u"name"_s, packNamespace);
    set.profession = reader.name(u"profession"_s, packNamespace);
    reader.context = u"trades '%1'"_s.arg(set.name);
    QcContentFields::checkFormatVersion(QcContentFields::villageFormatVersion, reader);
    if (set.name.isEmpty() || set.profession.isEmpty()) {
        reader.fail(u"needs a 'name' and a 'profession'"_s);
    }

    const QJsonArray offers = json.value(u"offers"_s).toArray();
    if (reader.ok() && (offers.isEmpty() || offers.size() > QcTradeSet::maxOffers)) {
        reader.fail(u"'offers' needs 1 to %1 entries"_s.arg(QcTradeSet::maxOffers));
    }
    for (const auto &entry : offers) {
        QcTradeOffer offer = readOffer(entry, packNamespace, reader);
        if (reader.ok() && std::ranges::any_of(set.offers, [&offer](const QcTradeOffer &other) { return other.id == offer.id; })) {
            reader.fail(u"offer '%1' appears twice (give one an 'id')"_s.arg(offer.id));
        }
        set.offers.append(std::move(offer));
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return set;
}

QcTradingSettings readTradingSettings(QcJsonFieldReader &reader)
{
    QcTradingSettings settings;
    reader.nestedObject(u"trading"_s, [&settings](const QJsonObject &object, QcJsonFieldReader &nested) {
        const QJsonArray levels = object.value(u"levels"_s).toArray();
        if (object.contains(u"levels"_s) && levels.size() != QcTradingSettings::levelCount) {
            nested.fail(u"'levels' lists the experience of all %1 levels"_s.arg(QcTradingSettings::levelCount));
        }
        if (levels.size() == QcTradingSettings::levelCount) {
            settings.levels.clear();
            for (const auto &level : levels) {
                settings.levels.append(level.toInt(-1));
            }
        }
        const bool rising = std::ranges::adjacent_find(std::as_const(settings.levels), std::greater_equal<>()) == settings.levels.cend();
        if (settings.levels.constFirst() != 0 || !rising || settings.levels.constLast() > (1 << 20)) {
            nested.fail(u"'levels' starts at 0 and rises (at most 1048576)"_s);
        }
        settings.offersPerLevel = nested.integer(u"offersPerLevel"_s, settings.offersPerLevel, 1, 8);
        settings.restocksPerDay = nested.integer(u"restocksPerDay"_s, settings.restocksPerDay, 0, 8);
        settings.restockGap = nested.number(u"restockGap"_s, settings.restockGap, 0.0, 1.0);
    });
    return settings;
}

} // namespace QcTradeParser
