#include "QcItemParser.h"

#include "QcArmourParser.h"
#include "QcContentFields.h"
#include "QcJsonFieldReader.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcItemParser {

QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"item"_s, {}};
    QcItemDefinition item;
    item.name = reader.name(u"name"_s, packNamespace);
    if (item.name.isEmpty()) {
        return qcError(reader.ok() ? u"item: 'name' is required"_s : reader.error);
    }

    reader.context = u"item '%1'"_s.arg(item.name);
    item.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"item"_s, item.name));
    item.descriptionKey = reader.string(u"description"_s, item.translationKey + u".description"_s);

    item.texture = reader.name(u"texture"_s, packNamespace, item.name);
    item.maxStack = reader.integer(u"maxStack"_s, 64, 1, QcItemDefinition::maxStackLimit);
    item.placesBlock = reader.name(u"places"_s, packNamespace);
    item.tool = reader.string(u"tool"_s);
    item.toolLevel = reader.integer(u"toolLevel"_s, 0, 0, 100);
    item.toolSpeed = reader.number(u"toolSpeed"_s, 1.0, 0.1, 100.0);
    item.durability = reader.integer(u"durability"_s, 0, 0, 100000);
    item.attackDamage = reader.number(u"attackDamage"_s, 1.0, 0.0, 1000.0);
    // Whole food points; larger values (older content) fill the bar completely.
    item.food = int(std::lround(std::min(reader.number(u"food"_s, 0.0, 0.0, 100.0), double(QcItemDefinition::maxFood))));
    item.fuelSeconds = reader.number(u"fuelSeconds"_s, 0.0, 0.0, QcContentFields::maxFuelSeconds);
    item.tags = QcContentFields::parseTags(json.value(u"tags"_s), packNamespace, reader);
    item.scoops = reader.boolean(u"scoops"_s, false);
    item.bottles = reader.boolean(u"bottles"_s, false);
    item.emptiesTo = reader.name(u"emptiesTo"_s, packNamespace);
    item.placesStand = reader.boolean(u"placesStand"_s, false);
    item.tills = reader.boolean(u"tills"_s, false);
    reader.nestedObject(u"armour"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        item.armour = QcArmourParser::parseArmour(object, packNamespace, item.name, nested);
    });

    if (!item.tool.isEmpty() && item.maxStack != 1 && !json.contains(u"maxStack"_s)) {
        item.maxStack = 1;
    }
    // An equipment slot holds one item.
    if (item.armour) {
        item.maxStack = 1;
    }
    // Burnt fuel leaves its container in the fuel slot, which has room for it only after the last item.
    if (item.fuelSeconds > 0.0 && !item.emptiesTo.isEmpty() && item.maxStack != 1) {
        reader.fail(u"fuel with 'emptiesTo' (a container) needs 'maxStack' 1"_s);
    }

    // One click either takes the liquid (bucket) or fills from it (bottle); never both.
    if (item.scoops && item.bottles) {
        reader.fail(u"'scoops' and 'bottles' exclude each other"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return item;
}

} // namespace QcItemParser
