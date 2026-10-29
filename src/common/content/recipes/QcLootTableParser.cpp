// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/recipes/QcLootTableParser.h"

#include "common/content/items/QcItemDefinition.h"
#include "common/content/registry/QcJsonFieldReader.h"

#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A whole number or [min, max] of whole numbers in [low, high] at `key`; `fallback` without the key.
std::pair<int, int> readRange(QcJsonFieldReader &reader, const QString &key, int fallback, int low, int high)
{
    const QJsonValue value = reader.json.value(key);
    if (value.isUndefined()) {
        return {fallback, fallback};
    }
    const auto whole = [low, high](const QJsonValue &number) {
        const double v = number.toDouble(-1.0);
        return number.isDouble() && std::floor(v) == v && v >= low && v <= high;
    };
    if (whole(value)) {
        return {value.toInt(), value.toInt()};
    }
    const QJsonArray range = value.toArray();
    if (range.size() == 2 && whole(range[0]) && whole(range[1]) && range[0].toInt() <= range[1].toInt()) {
        return {range[0].toInt(), range[1].toInt()};
    }
    reader.fail(u"'%1' must be a whole number or [min, max] in %2..%3"_s.arg(key).arg(low).arg(high));
    return {fallback, fallback};
}

QcLootTableDefinition::Entry parseEntry(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcLootTableDefinition::Entry entry;
    entry.item = reader.name(u"item"_s, packNamespace);
    entry.weight = reader.integer(u"weight"_s, 1, 0, QcLootTableDefinition::maxWeight);
    const auto [low, high] = readRange(reader, u"count"_s, 1, 1, QcItemDefinition::maxStackLimit);
    entry.minCount = low;
    entry.maxCount = high;
    return entry;
}

QcLootTableDefinition::Pool parsePool(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcLootTableDefinition::Pool pool;
    const auto [low, high] = readRange(reader, u"rolls"_s, 1, 0, QcLootTableDefinition::maxRolls);
    pool.minRolls = low;
    pool.maxRolls = high;
    const QJsonArray entries = reader.json.value(u"entries"_s).toArray();
    if (entries.isEmpty() || entries.size() > QcLootTableDefinition::maxEntries) {
        reader.fail(u"'entries' must be a list of 1..%1 objects"_s.arg(QcLootTableDefinition::maxEntries));
        return pool;
    }
    for (qsizetype i = 0; i < entries.size() && reader.ok(); ++i) {
        const QJsonObject object = entries[i].toObject();
        QcJsonFieldReader nested{object, u"%1 entry %2"_s.arg(reader.context).arg(i), {}};
        if (!entries[i].isObject()) {
            nested.fail(u"must be an object"_s);
        }
        pool.entries.append(parseEntry(nested, packNamespace));
        reader.adopt(nested);
    }
    return pool;
}

} // namespace

namespace QcLootTableParser {

QcResult<QcLootTableDefinition> parseLootTable(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"loot table"_s, {}};
    QcLootTableDefinition table;
    table.name = reader.name(u"name"_s, packNamespace);
    if (table.name.isEmpty()) {
        return qcError(reader.ok() ? u"loot table: 'name' is required"_s : reader.error);
    }

    reader.context = u"loot table '%1'"_s.arg(table.name);
    const QJsonArray pools = json.value(u"pools"_s).toArray();
    if (pools.isEmpty() || pools.size() > QcLootTableDefinition::maxPools) {
        return qcError(u"%1: 'pools' must be a list of 1..%2 objects"_s.arg(reader.context).arg(QcLootTableDefinition::maxPools));
    }
    for (qsizetype i = 0; i < pools.size() && reader.ok(); ++i) {
        const QJsonObject object = pools[i].toObject();
        QcJsonFieldReader nested{object, u"%1 pool %2"_s.arg(reader.context).arg(i), {}};
        if (!pools[i].isObject()) {
            nested.fail(u"must be an object"_s);
        }
        table.pools.append(parsePool(nested, packNamespace));
        reader.adopt(nested);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return table;
}

} // namespace QcLootTableParser
