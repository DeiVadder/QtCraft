#include "QcVillageLayoutParser.h"

#include <QJsonArray>

#include <array>

using namespace Qt::StringLiterals;

namespace {

using Kind = QcVillageLayout::Kind;

constexpr std::array<std::pair<Kind, const char16_t *>, 6> kindNames{{
    {Kind::Cross, u"cross"},
    {Kind::Street, u"street"},
    {Kind::Tee, u"t"},
    {Kind::Ell, u"l"},
    {Kind::Plaza, u"plaza"},
    {Kind::Tree, u"tree"},
}};

// A range under `key`: one number (both ends) or an array of two, the first not above the second,
// both within [low, high]; `fallback` without the key.
template<typename Value>
QcVillageRange<Value> readRange(const QString &key, QcVillageRange<Value> fallback, std::pair<Value, Value> bounds, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(key);
    if (value.isUndefined()) {
        return fallback;
    }

    const QJsonArray array = value.isArray() ? value.toArray() : QJsonArray{value, value};
    const auto inside = [&bounds](const QJsonValue &end) {
        return end.isDouble() && end.toDouble() >= double(bounds.first) && end.toDouble() <= double(bounds.second);
    };
    if (array.size() != 2 || !inside(array[0]) || !inside(array[1]) || array[0].toDouble() > array[1].toDouble()) {
        reader.fail(u"'%1' must be a number or [low, high] within %2 to %3"_s.arg(key).arg(double(bounds.first)).arg(double(bounds.second)));
        return fallback;
    }
    return {Value(array[0].toDouble()), Value(array[1].toDouble())};
}

// One entry of `layouts`: "kind" (required), "weight" and the kind's parameters over its defaults.
std::optional<QcVillageLayout> readLayout(const QJsonValue &value, QcJsonFieldReader &reader)
{
    QcJsonFieldReader nested{value.toObject(), reader.context + u" layout"_s, {}};
    const std::optional<Kind> kind = QcVillageLayoutParser::kindNamed(nested.string(u"kind"_s));
    if (!value.isObject() || !kind) {
        reader.fail(u"every layout needs a 'kind': cross, street, t, l, plaza or tree"_s);
        return std::nullopt;
    }

    QcVillageLayout layout = QcVillageLayoutParser::defaults(*kind);
    layout.weight = nested.integer(u"weight"_s, layout.weight, 1, 1000);
    layout.armLength = readRange(u"armLength"_s, layout.armLength, {0.2, 1.0}, nested);
    layout.arms = readRange(u"arms"_s, layout.arms, {2, 5}, nested);
    layout.bends = readRange(u"bends"_s, layout.bends, {0, 2}, nested);
    layout.ring = readRange(u"ring"_s, layout.ring, {8, 20}, nested);
    layout.exits = readRange(u"exits"_s, layout.exits, {1, 4}, nested);
    layout.winding = nested.number(u"winding"_s, layout.winding, 0.0, 1.0);
    layout.diagonals = nested.boolean(u"diagonals"_s, layout.diagonals);
    nested.nestedObject(u"branches"_s, [&layout](const QJsonObject &, QcJsonFieldReader &branches) {
        layout.branchChance = branches.number(u"chance"_s, layout.branchChance, 0.0, 1.0);
        layout.branchEvery = branches.integer(u"every"_s, layout.branchEvery, 0, 64);
        layout.branchLength = readRange(u"length"_s, layout.branchLength, {0.1, 1.0}, branches);
    });
    reader.adopt(nested);
    return layout;
}

} // namespace

namespace QcVillageLayoutParser {

QcVillageLayout defaults(Kind kind)
{
    QcVillageLayout layout;
    layout.kind = kind;
    switch (kind) {
    case Kind::Cross:
        break;
    case Kind::Street:
        layout.armLength = {0.8, 1.0};
        layout.branchChance = 0.8;
        layout.branchEvery = 12;
        layout.branchLength = {0.3, 0.55};
        break;
    case Kind::Tee:
        layout.armLength = {0.6, 1.0};
        break;
    case Kind::Ell:
        layout.armLength = {0.75, 1.0};
        layout.branchChance = 0.7;
        break;
    case Kind::Plaza:
        layout.armLength = {0.55, 0.9};
        layout.branchChance = 0.5;
        break;
    case Kind::Tree:
        layout.armLength = {0.6, 1.0};
        layout.branchChance = 0.4;
        layout.branchLength = {0.25, 0.45};
        break;
    }
    return layout;
}

QList<QcVillageLayout> defaultLayouts()
{
    QList<QcVillageLayout> layouts;
    for (const auto &[kind, weight] : {std::pair{Kind::Street, 3}, {Kind::Cross, 2}, {Kind::Tee, 2}, {Kind::Ell, 1}, {Kind::Plaza, 1}, {Kind::Tree, 2}}) {
        QcVillageLayout layout = defaults(kind);
        layout.weight = weight;
        layout.winding = kind == Kind::Street || kind == Kind::Tree ? 0.3 : 0.0;
        layouts.append(layout);
    }
    return layouts;
}

QString kindName(Kind kind)
{
    for (const auto &[entry, name] : kindNames) {
        if (entry == kind) {
            return QString::fromUtf16(name);
        }
    }
    return {};
}

std::optional<Kind> kindNamed(const QString &name)
{
    for (const auto &[kind, entry] : kindNames) {
        if (name == QStringView(entry)) {
            return kind;
        }
    }
    return std::nullopt;
}

QList<QcVillageLayout> read(const QJsonObject &style, QcJsonFieldReader &reader)
{
    const QJsonValue value = style.value(u"layouts"_s);
    if (value.isUndefined()) {
        return defaultLayouts();
    }
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.isEmpty() || array.size() > QcVillageStyleDefinition::maxLayouts) {
        reader.fail(u"'layouts' must be an array of 1 to %1 layouts"_s.arg(QcVillageStyleDefinition::maxLayouts));
        return defaultLayouts();
    }

    QList<QcVillageLayout> layouts;
    for (const auto &entry : array) {
        if (const std::optional<QcVillageLayout> layout = readLayout(entry, reader)) {
            layouts.append(*layout);
        }
    }
    return layouts;
}

} // namespace QcVillageLayoutParser
