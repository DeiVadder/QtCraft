// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcDimensionParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcWorldConstants.h"

#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Definition = QcDimensionDefinition;

// "sky": overworld, nether or end.
Definition::Sky skyOf(QcJsonFieldReader &reader)
{
    const QString sky = reader.string(u"sky"_s, u"overworld"_s);
    if (sky == u"nether"_s) {
        return Definition::Sky::Nether;
    }
    if (sky == u"end"_s) {
        return Definition::Sky::End;
    }
    if (sky != u"overworld"_s) {
        reader.fail(u"'sky' must be overworld, nether or end"_s);
    }
    return Definition::Sky::Overworld;
}

// "floor": bedrock or void.
Definition::Floor floorOf(QcJsonFieldReader &reader)
{
    const QString floor = reader.string(u"floor"_s, u"bedrock"_s);
    if (floor == u"void"_s) {
        return Definition::Floor::Void;
    }
    if (floor != u"bedrock"_s) {
        reader.fail(u"'floor' must be bedrock or void"_s);
    }
    return Definition::Floor::Bedrock;
}

// An optional whole number of a liquid override.
std::optional<int> optionalInteger(QcJsonFieldReader &reader, const QString &key, int low, int high)
{
    if (!reader.json.contains(key)) {
        return std::nullopt;
    }
    return reader.integer(key, low, low, high);
}

// "liquids": {"<liquid block>": {"distance": n, "ticks": n}}.
QList<Definition::LiquidFlow> liquidFlowsOf(QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue value = reader.json.value(u"liquids"_s);
    if (value.isUndefined()) {
        return {};
    }
    if (!value.isObject() || value.toObject().size() > Definition::maxLiquidFlows) {
        reader.fail(u"'liquids' must be an object of at most %1 liquids"_s.arg(Definition::maxLiquidFlows));
        return {};
    }

    QList<Definition::LiquidFlow> flows;
    const QJsonObject liquids = value.toObject();
    for (auto it = liquids.begin(); it != liquids.end(); ++it) {
        const QJsonObject entry{{u"liquid"_s, it.key()}};
        QcJsonFieldReader names{entry, reader.context, {}};
        Definition::LiquidFlow flow{names.name(u"liquid"_s, packNamespace), {}, {}};
        reader.adopt(names);
        if (!it.value().isObject()) {
            reader.fail(u"'liquids' entry '%1' must be an object"_s.arg(it.key()));
            continue;
        }
        const QJsonObject values = it.value().toObject();
        QcJsonFieldReader nested{values, reader.context + u" liquids "_s + it.key(), {}};
        flow.distance = optionalInteger(nested, u"distance"_s, 0, 15);
        flow.ticks = optionalInteger(nested, u"ticks"_s, 1, 1200);
        reader.adopt(nested);
        flows.append(std::move(flow));
    }
    return flows;
}

// "arrival": {"x", "y", "z", "platform", "platformRadius"}; none without the field.
std::optional<Definition::Arrival> arrivalOf(QcJsonFieldReader &reader, const QString &packNamespace)
{
    std::optional<Definition::Arrival> read;
    reader.nestedObject(u"arrival"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        constexpr int far = 30'000'000;
        Definition::Arrival arrival;
        arrival.position.x = nested.integer(u"x"_s, 0, -far, far);
        arrival.position.y = nested.integer(u"y"_s, 64, 1, QcWorld::worldHeight - 3);
        arrival.position.z = nested.integer(u"z"_s, 0, -far, far);
        arrival.platform = nested.name(u"platform"_s, packNamespace);
        arrival.platformRadius = nested.integer(u"platformRadius"_s, arrival.platformRadius, 0, Definition::maxPlatformRadius);
        read = arrival;
    });
    return read;
}

// What "generator" names.
struct Generator
{
    QString type;
    QString options;
};

// "generator": {"type", "options"} of `dimension`; its default type without the field.
Generator generatorOf(QcJsonFieldReader &reader, const Definition &dimension)
{
    Generator generator{dimension.generatorType, {}};
    reader.nestedObject(u"generator"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        generator.type = nested.string(u"type"_s, generator.type);
        generator.options = nested.string(u"options"_s);
    });
    if (generator.type.isEmpty() || generator.type.size() > 64) {
        reader.fail(u"'generator' 'type' must name a world type"_s);
    }
    if (generator.type == QLatin1StringView(Definition::worldGenerator) && !dimension.isOverworld()) {
        reader.fail(u"only %1 generates as the world's own type"_s.arg(QLatin1StringView(Definition::overworldName)));
    }
    return generator;
}

} // namespace

namespace QcDimensionParser {

QcResult<QcDimensionDefinition> parseDimension(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"dimension"_s, {}};
    Definition dimension;
    dimension.name = reader.name(u"name"_s, packNamespace);
    if (dimension.name.isEmpty()) {
        return qcError(reader.ok() ? u"dimension: 'name' is required"_s : reader.error);
    }

    // World.json and player files keep the name: what they refuse must not load.
    if (dimension.name.size() > Definition::maxNameLength) {
        return qcError(u"dimension: the name is too long, at most %1 characters with the namespace"_s.arg(Definition::maxNameLength));
    }

    // The name becomes a folder of the world (dimensions/<namespace>/<name>): no '/' or '.'.
    if (dimension.name.contains(u'/') || dimension.name.contains(u'.')) {
        return qcError(u"dimension '%1': the name must not contain '/' or '.'"_s.arg(dimension.name));
    }
    reader.context = u"dimension '%1'"_s.arg(dimension.name);
    dimension.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"dimension"_s, dimension.name));
    Generator generator = generatorOf(reader, dimension);
    dimension.generatorType = std::move(generator.type);
    dimension.generatorOptions = std::move(generator.options);
    dimension.ceiling = reader.integer(u"ceiling"_s, 0, 0, QcWorld::worldHeight - 1);
    dimension.coordinateScale = reader.number(u"coordinateScale"_s, 1.0, 1.0 / Definition::maxCoordinateScale, Definition::maxCoordinateScale);
    dimension.sky = skyOf(reader);
    dimension.fogColour = reader.colour(u"fogColour"_s);
    dimension.ambientLight = reader.integer(u"ambientLight"_s, 0, 0, 15);
    dimension.skyLight = reader.boolean(u"skyLight"_s, true);
    dimension.dayCycle = reader.boolean(u"dayCycle"_s, true);
    dimension.weather = reader.boolean(u"weather"_s, true);
    dimension.waterEvaporates = reader.boolean(u"waterEvaporates"_s, false);
    dimension.liquidFlows = liquidFlowsOf(reader, packNamespace);
    dimension.bedsExplode = reader.boolean(u"bedsExplode"_s, false);
    dimension.compassesSpin = reader.boolean(u"compassesSpin"_s, false);
    dimension.floor = floorOf(reader);
    dimension.voidDamage = reader.number(u"voidDamage"_s, dimension.voidDamage, 0.0, Definition::maxVoidDamage);
    dimension.arrival = arrivalOf(reader, packNamespace);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return dimension;
}

} // namespace QcDimensionParser
