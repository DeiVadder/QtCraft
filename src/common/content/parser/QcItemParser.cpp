#include "common/content/parser/QcItemParser.h"

#include "common/content/parser/QcArmourParser.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Reads one item definition: owns the definition being built and the reader of the item's JSON object.
// The parts run in a fixed order, later parts check fields that earlier parts read.
class ItemReader
{
public:
    ItemReader(const QJsonObject &json, const QString &packNamespace)
        : m_json(json)
        , m_namespace(packNamespace)
        , m_reader{json, u"item"_s, {}}
    {
    }

    [[nodiscard]] QcResult<QcItemDefinition> read();

private:
    void parseBaseFields();
    void parseBlockUses();
    void parseProjectileUses();
    void parseDispense();
    void parseContainerUses();
    void parseFishing();
    void applyStackRules();

    const QJsonObject &m_json;
    const QString &m_namespace;
    QcJsonFieldReader m_reader;
    QcItemDefinition m_item;
};

// What it does used on a block besides placing one: hoe, shovel, flint and steel, bone meal.
void ItemReader::parseBlockUses()
{
    m_item.tills = m_reader.boolean(u"tills"_s, false);
    m_item.flattens = m_reader.boolean(u"flattens"_s, false);
    m_item.carves = m_reader.boolean(u"carves"_s, false);
    m_item.strips = m_reader.boolean(u"strips"_s, false);
    m_item.dye = m_reader.string(u"dye"_s);
    const QString dial = m_reader.string(u"dial"_s);
    if (dial == u"compass"_s) {
        m_item.dial = QcItemDial::Compass;
    } else if (dial == u"clock"_s) {
        m_item.dial = QcItemDial::Clock;
    } else if (!dial.isEmpty()) {
        m_reader.fail(u"'dial' must be \"compass\" or \"clock\""_s);
    }
    m_item.ignites = m_reader.boolean(u"ignites"_s, false);
    if (m_item.ignites && (!m_item.placesBlock.isEmpty() || m_item.tills || m_item.flattens || m_item.scoops || m_item.bottles)) {
        m_reader.fail(u"an item that 'ignites' neither places, tills, flattens, scoops nor bottles"_s);
    }
    m_item.fertilizes = m_reader.boolean(u"fertilizes"_s, false);
    if (m_item.fertilizes && (!m_item.placesBlock.isEmpty() || m_item.ignites)) {
        m_reader.fail(u"an item that 'fertilizes' neither places nor ignites"_s);
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
void ItemReader::parseProjectileUses()
{
    m_reader.nestedObject(u"launcher"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        QcLauncherDefinition launcher;
        launcher.drawSeconds = nested.number(u"drawSeconds"_s, launcher.drawSeconds, 0.05, 10.0);
        launcher.speed = nested.number(u"speed"_s, launcher.speed, 1.0, QcProjectileDefinition::maxSpeed);
        launcher.minPower = nested.number(u"minPower"_s, launcher.minPower, 0.0, 1.0);
        launcher.ammunition = parseAmmunition(m_namespace, nested);
        const QJsonValue listed = nested.json.value(u"ammunition"_s);
        launcher.emptyAmmunitionList = listed.isArray() && listed.toArray().isEmpty();
        m_item.launcher = launcher;
    });
    m_item.projectile = m_reader.name(u"projectile"_s, m_namespace);
    m_reader.nestedObject(u"throwable"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        QcThrowableDefinition throwable;
        throwable.speed = nested.number(u"speed"_s, throwable.speed, 1.0, QcProjectileDefinition::maxSpeed);
        throwable.cooldownSeconds = nested.number(u"cooldownSeconds"_s, throwable.cooldownSeconds, 0.0, 60.0);
        throwable.inaccuracyDegrees = nested.number(u"inaccuracyDegrees"_s, throwable.inaccuracyDegrees, 0.0, 45.0);
        m_item.throwable = throwable;
    });
    if (m_item.throwable && (m_item.launcher || !m_item.placesBlock.isEmpty() || m_item.food > 0 || m_item.armour)) {
        m_reader.fail(u"a 'throwable' item neither launches, places, is eaten nor worn"_s);
    }
    if (m_item.launcher && (!m_item.placesBlock.isEmpty() || m_item.food > 0 || m_item.armour)) {
        m_reader.fail(u"a 'launcher' neither places, is eaten nor worn"_s);
    }
}

// "dispense": throw (default), place_liquid, scoop, equip, ignite or shoot, checked against what the item is.
void ItemReader::parseDispense()
{
    const QString action = m_reader.string(u"dispense"_s, u"throw"_s);
    if (action == u"place_liquid"_s) {
        m_item.dispense = QcDispenseAction::PlaceLiquid;
        if (m_item.placesBlock.isEmpty() || m_item.emptiesTo.isEmpty()) {
            m_reader.fail(u"'dispense': place_liquid needs 'places' and 'emptiesTo'"_s);
        }
    } else if (action == u"scoop"_s) {
        m_item.dispense = QcDispenseAction::Scoop;
        if (!m_item.scoops) {
            m_reader.fail(u"'dispense': scoop needs 'scoops'"_s);
        }
    } else if (action == u"equip"_s) {
        m_item.dispense = QcDispenseAction::Equip;
        if (!m_item.armour) {
            m_reader.fail(u"'dispense': equip needs 'armour'"_s);
        }
    } else if (action == u"ignite"_s) {
        m_item.dispense = QcDispenseAction::Ignite;
        if (!m_item.ignites) {
            m_reader.fail(u"'dispense': ignite needs 'ignites'"_s);
        }
    } else if (action == u"shoot"_s) {
        m_item.dispense = QcDispenseAction::Shoot;
        if (m_item.projectile.isEmpty()) {
            m_reader.fail(u"'dispense': shoot needs 'projectile'"_s);
        }
    } else if (action != u"throw"_s) {
        m_reader.fail(u"'dispense' must be throw, place_liquid, scoop, equip, ignite or shoot"_s);
    }
}

// Drinks and fish buckets (decisions E-500, E-501): "drink": {"clearsEffects": true} and "releases".
void ItemReader::parseContainerUses()
{
    m_reader.nestedObject(u"drink"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        QcDrinkDefinition drink;
        drink.clearsEffects = nested.boolean(u"clearsEffects"_s, false);
        m_item.drink = drink;
    });
    if (m_item.drink && (m_item.food > 0 || m_item.armour || !m_item.placesBlock.isEmpty() || m_item.throwable || m_item.launcher)) {
        m_reader.fail(u"a 'drink' is neither eaten, worn, placed, thrown nor launches"_s);
    }
    m_item.releasesMob = m_reader.name(u"releases"_s, m_namespace);
    if (!m_item.releasesMob.isEmpty() && m_item.placesBlock.isEmpty()) {
        m_reader.fail(u"'releases' needs 'places' (the liquid the creature comes out in)"_s);
    }
}

// The loot table of a fishing rod: [{"item", "weight", "count"}, ...] (decision E-503).
QList<QcFishingLoot> parseFishingLoot(const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"loot"_s);
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.isEmpty() || list.size() > 64) {
        reader.fail(u"'loot' must be an array of 1 to 64 entries"_s);
        return {};
    }

    QList<QcFishingLoot> loot;
    for (const auto &entry : list) {
        QcJsonFieldReader nested{entry.toObject(), reader.context + u" loot"_s, {}};
        QcFishingLoot one;
        one.item = nested.name(u"item"_s, packNamespace);
        if (!entry.isObject() || one.item.isEmpty()) {
            nested.fail(u"every entry needs an 'item'"_s);
        }
        one.weight = nested.integer(u"weight"_s, one.weight, 1, 10000);
        one.count = nested.integer(u"count"_s, one.count, 1, 64);
        reader.adopt(nested);
        loot.append(one);
    }
    return loot;
}

// "fishing": a fishing rod's bobber, wait, bite and loot (decision E-503).
void ItemReader::parseFishing()
{
    m_reader.nestedObject(u"fishing"_s, [&](const QJsonObject &, QcJsonFieldReader &nested) {
        QcFishingDefinition fishing;
        fishing.bobber = nested.name(u"bobber"_s, m_namespace);
        if (fishing.bobber.isEmpty()) {
            nested.fail(u"'bobber' (a projectile kind) is required"_s);
        }
        fishing.speed = nested.number(u"speed"_s, fishing.speed, 1.0, QcProjectileDefinition::maxSpeed);
        fishing.minSeconds = nested.number(u"minSeconds"_s, fishing.minSeconds, 0.05, 3600.0);
        fishing.maxSeconds = nested.number(u"maxSeconds"_s, fishing.maxSeconds, fishing.minSeconds, 3600.0);
        fishing.rainFactor = nested.number(u"rainFactor"_s, fishing.rainFactor, 0.01, 10.0);
        fishing.biteSeconds = nested.number(u"biteSeconds"_s, fishing.biteSeconds, 0.05, 60.0);
        fishing.maxDistance = nested.number(u"maxDistance"_s, fishing.maxDistance, 1.0, 128.0);
        fishing.castSound = nested.name(u"castSound"_s, m_namespace);
        fishing.biteSound = nested.name(u"biteSound"_s, m_namespace);
        fishing.loot = parseFishingLoot(m_namespace, nested);
        m_item.fishing = fishing;
    });
    if (m_item.fishing && (m_item.throwable || m_item.launcher || !m_item.placesBlock.isEmpty() || m_item.food > 0 || m_item.armour || m_item.drink)) {
        m_reader.fail(u"a 'fishing' rod neither throws, launches, places, is eaten, drunk nor worn"_s);
    }
}

void ItemReader::parseBaseFields()
{
    m_item.texture = m_reader.name(u"texture"_s, m_namespace, m_item.name);
    m_item.maxStack = m_reader.integer(u"maxStack"_s, 64, 1, QcItemDefinition::maxStackLimit);
    m_item.placesBlock = m_reader.name(u"places"_s, m_namespace);
    m_item.tool = m_reader.string(u"tool"_s);
    m_item.toolLevel = m_reader.integer(u"toolLevel"_s, 0, 0, 100);
    m_item.toolSpeed = m_reader.number(u"toolSpeed"_s, 1.0, 0.1, 100.0);
    m_item.durability = m_reader.integer(u"durability"_s, 0, 0, 100000);
    m_item.attackDamage = m_reader.number(u"attackDamage"_s, 1.0, 0.0, 1000.0);
    // Whole food points; larger values (older content) fill the bar completely.
    m_item.food = int(std::lround(std::min(m_reader.number(u"food"_s, 0.0, 0.0, 100.0), double(QcItemDefinition::maxFood))));
    m_item.fuelSeconds = m_reader.number(u"fuelSeconds"_s, 0.0, 0.0, QcContentFields::maxFuelSeconds);
    m_item.tags = QcContentFields::parseTags(m_json.value(u"tags"_s), m_namespace, m_reader);
    m_item.scoops = m_reader.boolean(u"scoops"_s, false);
    m_item.bottles = m_reader.boolean(u"bottles"_s, false);
    m_item.emptiesTo = m_reader.name(u"emptiesTo"_s, m_namespace);
    m_item.placesStand = m_reader.boolean(u"placesStand"_s, false);
    m_item.placesCart = m_reader.boolean(u"placesCart"_s, false);
    if (m_item.placesStand && m_item.placesCart) {
        m_reader.fail(u"an item places either an armour stand or a minecart"_s);
    }
    m_item.cartBlock = m_reader.name(u"cartBlock"_s, m_namespace);
    if (!m_item.cartBlock.isEmpty() && !m_item.placesCart) {
        m_reader.fail(u"only an item that places a minecart has a 'cartBlock'"_s);
    }
}

void ItemReader::applyStackRules()
{
    if (!m_item.tool.isEmpty() && m_item.maxStack != 1 && !m_json.contains(u"maxStack"_s)) {
        m_item.maxStack = 1;
    }
    // An equipment slot holds one item.
    if (m_item.armour) {
        m_item.maxStack = 1;
    }
    // Burnt fuel leaves its container in the fuel slot, which has room for it only after the last item.
    if (m_item.fuelSeconds > 0.0 && !m_item.emptiesTo.isEmpty() && m_item.maxStack != 1) {
        m_reader.fail(u"fuel with 'emptiesTo' (a container) needs 'maxStack' 1"_s);
    }

    // One click either takes the liquid (bucket) or fills from it (bottle); never both.
    if (m_item.scoops && m_item.bottles) {
        m_reader.fail(u"'scoops' and 'bottles' exclude each other"_s);
    }
}

QcResult<QcItemDefinition> ItemReader::read()
{
    m_item.name = m_reader.name(u"name"_s, m_namespace);
    if (m_item.name.isEmpty()) {
        return qcError(m_reader.ok() ? u"item: 'name' is required"_s : m_reader.error);
    }

    m_reader.context = u"item '%1'"_s.arg(m_item.name);
    m_item.translationKey = m_reader.string(u"translation"_s, QcContentFields::translationKey(u"item"_s, m_item.name));
    m_item.descriptionKey = m_reader.string(u"description"_s, m_item.translationKey + u".description"_s);

    parseBaseFields();
    parseBlockUses();
    m_reader.nestedObject(u"armour"_s, [this](const QJsonObject &object, QcJsonFieldReader &nested) {
        m_item.armour = QcArmourParser::parseArmour(object, m_namespace, m_item.name, nested);
    });

    parseProjectileUses();
    parseDispense();
    parseContainerUses();
    parseFishing();
    applyStackRules();

    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return std::move(m_item);
}

} // namespace

namespace QcItemParser {

[[nodiscard]] QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace)
{
    return ItemReader(json, packNamespace).read();
}

} // namespace QcItemParser
