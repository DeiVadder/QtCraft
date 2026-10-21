#include "common/content/parser/QcVegetationParser.h"

#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

// One entry of `plants`: a block name (weight 1) or {"block", "weight"}.
[[nodiscard]] QcVegetationPlant parsePlant(const QJsonValue &entry, QcJsonFieldReader &reader, const QString &packNamespace)
{
    if (entry.isString()) {
        return {QcContentNames::qualify(entry.toString(), packNamespace), 1};
    }
    if (!entry.isObject()) {
        reader.fail(u"'plants' entries are block names or objects"_s);
        return {};
    }

    const QJsonObject object = entry.toObject();
    QcJsonFieldReader nested{object, reader.context + u" plant"_s, {}};
    QcVegetationPlant plant{nested.name(u"block"_s, packNamespace), nested.integer(u"weight"_s, 1, 1, QcVegetationLayer::maxWeight)};
    reader.adopt(nested);
    return plant;
}

[[nodiscard]] QcVegetationPatches parsePatches(QcJsonFieldReader &reader)
{
    QcVegetationPatches patches;
    patches.chance = reader.number(u"chance"_s, patches.chance, 0.0, 1.0);
    patches.tries = reader.integer(u"tries"_s, patches.tries, 1, 128);
    patches.radius = reader.integer(u"radius"_s, patches.radius, 1, QcVegetationPatches::maxRadius);
    return patches;
}

[[nodiscard]] QcVegetationLayer parseLayer(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcVegetationLayer layer;
    layer.density = reader.number(u"density"_s, 0.0, 0.0, 1.0);
    layer.patchSize = reader.integer(u"patchSize"_s, 0, 0, 512);
    reader.nestedObject(u"patches"_s, [&layer](const QJsonObject &, QcJsonFieldReader &nested) { layer.patches = parsePatches(nested); });

    const QJsonValue plants = json.value(u"plants"_s);
    if (!plants.isArray() || plants.toArray().isEmpty() || plants.toArray().size() > QcVegetationLayer::maxPlants) {
        reader.fail(u"'plants' must be an array of 1 to %1 plants"_s.arg(QcVegetationLayer::maxPlants));
        return layer;
    }

    const QJsonArray list = plants.toArray();
    for (const auto &entry : list) {
        const QcVegetationPlant plant = parsePlant(entry, reader, packNamespace);
        if (!reader.ok()) {
            break;
        }
        if (plant.block.isEmpty()) {
            reader.fail(u"a plant needs a 'block'"_s);
            break;
        }
        layer.plants.append(plant);
    }
    return layer;
}

} // namespace

namespace QcVegetationParser {

QcVegetationDefinition parse(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcVegetationDefinition vegetation;
    reader.nestedObject(u"grass"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        vegetation.grass = parseLayer(object, nested, packNamespace);
    });
    reader.nestedObject(u"flowers"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        vegetation.flowers = parseLayer(object, nested, packNamespace);
    });
    return vegetation;
}

} // namespace QcVegetationParser
