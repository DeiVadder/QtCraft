#include "common/content/parser/QcItemParser.h"

#include "common/content/parser/QcArmourParser.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// "dispense": throw (default), place_liquid, scoop, equip or ignite, checked against what the item is.
// What it does used on a block besides placing one: hoe, shovel, flint and steel, bone meal.
void parseBlockUses(QcItemDefinition &item, QcJsonFieldReader &reader)
{
    item.tills = reader.boolean(u"tills"_s, false);
    item.flattens = reader.boolean(u"flattens"_s, false);
    item.carves = reader.boolean(u"carves"_s, false);
    item.ignites = reader.boolean(u"ignites"_s, false);
    if (item.ignites && (!item.placesBlock.isEmpty() || item.tills || item.flattens || item.scoops || item.bottles)) {
        reader.fail(u"an item that 'ignites' neither places, tills, flattens, scoops nor bottles"_s);
    }
    item.fertilizes = reader.boolean(u"fertilizes"_s, false);
    if (item.fertilizes && (!item.placesBlock.isEmpty() || item.ignites)) {
        reader.fail(u"an item that 'fertilizes' neither places nor ignites"_s);
    }
}

// "ammunition" of a launcher: item names and "#tag"s (decision E-427).
QStringList parseAmmunition(const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"ammunition"_s);
    const QJsonArray list = value.toArray();
    if (!value.isUndefined() && (!value.isArray() || list.size() > 64)) {
        reader.fail(u"'ammunition' must be an array of at most 64 item names or tags"_s);
        return {};
    }
    QStringList names;
    for (const auto &entry : list) {
        const QString name = entry.isString() ? QcContentFields::qualifyIngredient(entry.toString(), packNamespace) : QString();
        if (name.isEmpty()) {
            reader.fail(u"invalid 'ammunition' entry"_s);
            return {};
        }
        names.append(name);
    }
    return names;
}

// What the item does with projectiles: a launcher, ammunition, thrown by hand (decisions E-425, E-427).
void parseProjectileUses(QcItemDefinition &item, const QString &packNamespace, QcJsonFieldReader &reader)
{
    reader.nestedObject(u"launcher"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        QcLauncherDefinition launcher;
        launcher.drawSeconds = nested.number(u"drawSeconds"_s, launcher.drawSeconds, 0.05, 10.0);
        launcher.speed = nested.number(u"speed"_s, launcher.speed, 1.0, QcProjectileDefinition::maxSpeed);
        launcher.minPower = nested.number(u"minPower"_s, launcher.minPower, 0.0, 1.0);
        launcher.ammunition = parseAmmunition(packNamespace, nested);
        const QJsonValue listed = nested.json.value(u"ammunition"_s);
        launcher.emptyAmmunitionList = listed.isArray() && listed.toArray().isEmpty();
        item.launcher = launcher;
    });
    item.projectile = reader.name(u"projectile"_s, packNamespace);
    reader.nestedObject(u"throwable"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        QcThrowableDefinition throwable;
        throwable.speed = nested.number(u"speed"_s, throwable.speed, 1.0, QcProjectileDefinition::maxSpeed);
        throwable.cooldownSeconds = nested.number(u"cooldownSeconds"_s, throwable.cooldownSeconds, 0.0, 60.0);
        throwable.inaccuracyDegrees = nested.number(u"inaccuracyDegrees"_s, throwable.inaccuracyDegrees, 0.0, 45.0);
        item.throwable = throwable;
    });
    if (item.throwable && (item.launcher || !item.placesBlock.isEmpty() || item.food > 0 || item.armour)) {
        reader.fail(u"a 'throwable' item neither launches, places, is eaten nor worn"_s);
    }
    if (item.launcher && (!item.placesBlock.isEmpty() || item.food > 0 || item.armour)) {
        reader.fail(u"a 'launcher' neither places, is eaten nor worn"_s);
    }
}

void parseDispense(QcItemDefinition &item, QcJsonFieldReader &reader)
{
    const QString action = reader.string(u"dispense"_s, u"throw"_s);
    if (action == u"place_liquid"_s) {
        item.dispense = QcDispenseAction::PlaceLiquid;
        if (item.placesBlock.isEmpty() || item.emptiesTo.isEmpty()) {
            reader.fail(u"'dispense': place_liquid needs 'places' and 'emptiesTo'"_s);
        }
    } else if (action == u"scoop"_s) {
        item.dispense = QcDispenseAction::Scoop;
        if (!item.scoops) {
            reader.fail(u"'dispense': scoop needs 'scoops'"_s);
        }
    } else if (action == u"equip"_s) {
        item.dispense = QcDispenseAction::Equip;
        if (!item.armour) {
            reader.fail(u"'dispense': equip needs 'armour'"_s);
        }
    } else if (action == u"ignite"_s) {
        item.dispense = QcDispenseAction::Ignite;
        if (!item.ignites) {
            reader.fail(u"'dispense': ignite needs 'ignites'"_s);
        }
    } else if (action != u"throw"_s) {
        reader.fail(u"'dispense' must be throw, place_liquid, scoop, equip or ignite"_s);
    }
}

} // namespace

namespace QcItemParser {

[[nodiscard]] QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace)
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
    item.placesCart = reader.boolean(u"placesCart"_s, false);
    if (item.placesStand && item.placesCart) {
        reader.fail(u"an item places either an armour stand or a minecart"_s);
    }
    item.cartBlock = reader.name(u"cartBlock"_s, packNamespace);
    if (!item.cartBlock.isEmpty() && !item.placesCart) {
        reader.fail(u"only an item that places a minecart has a 'cartBlock'"_s);
    }
    parseBlockUses(item, reader);
    reader.nestedObject(u"armour"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        item.armour = QcArmourParser::parseArmour(object, packNamespace, item.name, nested);
    });

    parseDispense(item, reader);
    parseProjectileUses(item, packNamespace, reader);

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
