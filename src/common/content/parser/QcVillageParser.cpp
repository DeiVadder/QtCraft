// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcVillageParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcVillageLayoutParser.h"
#include "common/content/parser/QcVillagerParser.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

// An object of qualified name -> qualified name, at most `limit` entries; fails the reader otherwise.
QHash<QString, QString> nameMap(const QJsonObject &json, const QString &key, const QString &packNamespace, int limit,
                                QcJsonFieldReader &reader)
{
    QHash<QString, QString> map;
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return map;
    }
    if (!value.isObject() || value.toObject().size() > limit) {
        reader.fail(u"'%1' must be an object of at most %2 entries"_s.arg(key).arg(limit));
        return map;
    }

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString from = QcContentNames::qualify(it.key(), packNamespace);
        const QString to = it.value().isString() ? QcContentNames::qualify(it.value().toString(), packNamespace) : QString();
        if (from.isEmpty() || to.isEmpty()) {
            reader.fail(u"'%1' entry '%2' needs two valid names"_s.arg(key, it.key()));
            return map;
        }
        map.insert(from, to);
    }
    return map;
}

// The blueprints of a kind: "structure" (one) or "structures" ([{"structure", "weight"}]).
QList<QcVillageBlueprint> readBlueprints(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QList<QcVillageBlueprint> blueprints;
    if (json.contains(u"structure"_s)) {
        blueprints.append({reader.name(u"structure"_s, packNamespace), 1});
        return blueprints;
    }

    const QJsonArray array = json.value(u"structures"_s).toArray();
    if (!json.value(u"structures"_s).isArray() || array.isEmpty() || array.size() > QcVillagePiece::maxBlueprints) {
        reader.fail(u"every piece needs a 'structure' or 1 to %1 'structures'"_s.arg(QcVillagePiece::maxBlueprints));
        return blueprints;
    }
    for (const auto &value : array) {
        QcJsonFieldReader nested{value.toObject(), reader.context, {}};
        const QcVillageBlueprint blueprint{nested.name(u"structure"_s, packNamespace), nested.integer(u"weight"_s, 1, 1, 1000)};
        if (!value.isObject() || blueprint.structure.isEmpty()) {
            nested.fail(u"every entry of 'structures' needs a 'structure'"_s);
        }
        reader.adopt(nested);
        blueprints.append(blueprint);
    }
    return blueprints;
}

// The jetty of a kind at the water (decision E-408), oak by default.
[[nodiscard]] QcVillageJetty readJetty(const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcVillageJetty jetty;
    jetty.deck = u"qtcraft:oak_planks"_s;
    jetty.post = u"qtcraft:oak_log"_s;
    reader.nestedObject(u"jetty"_s, [&jetty, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        jetty.deck = nested.name(u"deck"_s, packNamespace, jetty.deck);
        jetty.post = nested.name(u"post"_s, packNamespace, jetty.post);
        jetty.width = nested.integer(u"width"_s, 1, 1, 5) | 1;
        jetty.over = nested.integer(u"over"_s, 4, 1, 16);
    });
    return jetty;
}

// The pieces of a style: [{"structure" or "structures", "weight", "max", rules}].
QList<QcVillagePiece> readPieces(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QList<QcVillagePiece> pieces;
    const QJsonArray array = json.value(u"pieces"_s).toArray();
    if (!json.value(u"pieces"_s).isArray() || array.isEmpty() || array.size() > QcVillageStyleDefinition::maxPieces) {
        reader.fail(u"'pieces' must be an array of 1 to %1 pieces"_s.arg(QcVillageStyleDefinition::maxPieces));
        return pieces;
    }

    for (const auto &value : array) {
        QcJsonFieldReader nested{value.toObject(), reader.context, {}};
        QcVillagePiece piece;
        piece.blueprints = readBlueprints(value.toObject(), packNamespace, nested);
        piece.weight = nested.integer(u"weight"_s, 1, 1, 1000);
        piece.max = nested.integer(u"max"_s, 0, 0, 64);
        // The rules of a kind (decision E-404, E-408): chance, minRoadLength, water with its jetty.
        piece.chance = nested.number(u"chance"_s, 1.0, 0.0, 1.0);
        piece.minRoadLength = nested.integer(u"minRoadLength"_s, 0, 0, 4096);
        piece.water = nested.integer(u"water"_s, 0, 0, 32);
        piece.jetty = readJetty(packNamespace, nested);
        const bool named = std::ranges::all_of(piece.blueprints, [](const QcVillageBlueprint &b) { return !b.structure.isEmpty(); });
        if (!value.isObject() || piece.blueprints.isEmpty() || !named) {
            nested.fail(u"every piece needs a 'structure'"_s);
        }
        reader.adopt(nested);
        pieces.append(piece);
    }
    return pieces;
}

// A block state "name[property=value,...]" with its name qualified (the state is checked when the
// style is resolved); empty when the name is invalid.
QString qualifyState(const QString &text, const QString &packNamespace)
{
    const qsizetype bracket = text.indexOf(u'[');
    const QString name = QcContentNames::qualify(bracket < 0 ? text : text.left(bracket), packNamespace);
    return name.isEmpty() || bracket < 0 ? name : name + text.mid(bracket);
}

// One entry of a list of weighted blocks: a block name, or an object with "block", "weight" and,
// for roads, "step". Road blocks and steps may be states (a top slab as a boardwalk). `what` names
// the list in errors.
template<typename Entry>
std::optional<Entry> readWeighted(const QJsonValue &value, const QString &packNamespace, const QString &what, QcJsonFieldReader &reader)
{
    Entry entry;
    constexpr bool road = requires { entry.step; };
    const auto block = [&packNamespace](const QString &text) {
        return road ? qualifyState(text, packNamespace) : QcContentNames::qualify(text, packNamespace);
    };
    if (value.isString()) {
        entry.block = block(value.toString());
    } else if (value.isObject()) {
        QcJsonFieldReader nested{value.toObject(), reader.context, {}};
        entry.block = block(nested.string(u"block"_s));
        entry.weight = nested.integer(u"weight"_s, 1, 1, 1000);
        if constexpr (road) {
            const QString step = nested.string(u"step"_s);
            entry.step = step.isEmpty() ? QString() : qualifyState(step, packNamespace);
            if (!step.isEmpty() && entry.step.isEmpty()) {
                nested.fail(u"'step' is not a valid block: '%1'"_s.arg(step));
            }
        }
        reader.adopt(nested);
    }
    if (entry.block.isEmpty()) {
        reader.fail(u"every entry of '%1' needs a block"_s.arg(what));
        return std::nullopt;
    }
    return entry;
}

// A list of weighted blocks under `key`: one entry (a name or an object) or an array of 1 to `limit`.
template<typename Entry>
QList<Entry> readWeightedList(const QJsonObject &json, const QString &key, const QString &packNamespace, int limit,
                              QcJsonFieldReader &reader)
{
    QList<Entry> entries;
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return entries;
    }
    const QJsonArray array = value.isArray() ? value.toArray() : QJsonArray{value};
    if (array.isEmpty() || array.size() > limit) {
        reader.fail(u"'%1' must hold 1 to %2 entries"_s.arg(key).arg(limit));
        return entries;
    }
    for (const auto &item : array) {
        if (const std::optional<Entry> entry = readWeighted<Entry>(item, packNamespace, key, reader)) {
            entries.append(*entry);
        }
    }
    return entries;
}

// Reads one village style over a default style or a copy of the base it extends: owns the style being
// built and the reader of its JSON object.
class StyleReader
{
public:
    StyleReader(QcVillageStyleDefinition style, const QJsonObject &json, const QString &packNamespace, bool extending)
        : m_json(json)
        , m_namespace(packNamespace)
        , m_extending(extending)
        , m_reader{json, u"village style '%1'"_s.arg(style.name), {}}
        , m_style(std::move(style))
    {
    }

    [[nodiscard]] QcResult<QcVillageStyleDefinition> read();

private:
    void readGround();
    void readPlacement();
    void readBuildings();
    void readAdditions();
    void readRemovals();

    const QJsonObject &m_json;
    const QString &m_namespace;
    const bool m_extending;
    QcJsonFieldReader m_reader;
    QcVillageStyleDefinition m_style;
};

// Where the style stands (decision E-407): "ground" "land" (default) or "water", the latter with its
// "stilts" (posts and stairs of spruce by default) and an optional "wetland" (the ground it shapes).
void StyleReader::readGround()
{
    const QString ground = m_reader.string(u"ground"_s, u"land"_s);
    if (ground != u"land"_s && ground != u"water"_s) {
        m_reader.fail(u"'ground' must be \"land\" or \"water\""_s);
        return;
    }
    m_style.ground = ground == u"water"_s ? QcVillageStyleDefinition::Ground::Water : QcVillageStyleDefinition::Ground::Land;

    QcVillageStilts &stilts = m_style.stilts;
    stilts.post = u"qtcraft:spruce_log"_s;
    stilts.stair = u"qtcraft:spruce_stairs"_s;
    m_reader.nestedObject(u"stilts"_s, [this, &stilts](const QJsonObject &, QcJsonFieldReader &nested) {
        stilts.height = nested.integer(u"height"_s, stilts.height, 2, 8);
        stilts.spacing = nested.integer(u"spacing"_s, stilts.spacing, 1, 8);
        stilts.maxDepth = nested.integer(u"maxDepth"_s, stilts.maxDepth, 0, 16);
        stilts.water = nested.number(u"water"_s, stilts.water, 0.0, 1.0);
        stilts.post = nested.name(u"post"_s, m_namespace, stilts.post);
        stilts.stair = nested.name(u"stair"_s, m_namespace, stilts.stair);
    });

    QcVillageWetland &wetland = m_style.wetland;
    wetland.bottom = u"qtcraft:mud"_s;
    m_reader.nestedObject(u"wetland"_s, [this, &wetland](const QJsonObject &, QcJsonFieldReader &nested) {
        wetland.enabled = true;
        wetland.depth = nested.integer(u"depth"_s, wetland.depth, 1, 4);
        wetland.margin = nested.integer(u"margin"_s, wetland.margin, 0, 12);
        wetland.fray = nested.integer(u"fray"_s, wetland.fray, 0, 8);
        wetland.blend = nested.integer(u"blend"_s, wetland.blend, 1, 8);
        wetland.bottom = nested.name(u"bottom"_s, m_namespace, wetland.bottom);
        wetland.water = nested.number(u"water"_s, wetland.water, 0.0, 1.0);
        wetland.mudBand = nested.integer(u"mudBand"_s, wetland.mudBand, 0, 4);
        wetland.scale = nested.number(u"scale"_s, wetland.scale, 2.0, 64.0);
        wetland.plant = nested.name(u"plant"_s, m_namespace, wetland.plant);
        wetland.plantChance = nested.number(u"plantChance"_s, wetland.plantChance, 0.0, 1.0);
        wetland.floatingPlant = nested.name(u"floatingPlant"_s, m_namespace, wetland.floatingPlant);
        wetland.floatingPlantChance = nested.number(u"floatingPlantChance"_s, wetland.floatingPlantChance, 0.0, 1.0);
    });
}

QStringList readBiomes(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QStringList biomes;
    const QJsonValue value = json.value(u"biomes"_s);
    if (!value.isUndefined() && !value.isArray()) {
        reader.fail(u"'biomes' must be an array of biome names"_s);
        return biomes;
    }
    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        const QString biome = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (biome.isEmpty()) {
            reader.fail(u"'biomes' must be an array of biome names"_s);
            return biomes;
        }
        biomes.append(biome);
    }
    return biomes;
}

// The ranges of the golem fields (decisions E-415, E-470).
constexpr int maxGolemVillagers = 1024;
constexpr double maxGolemDistance = 64.0;
constexpr double maxGolemSeconds = 86400.0;

// The golem fields of `nested`; the missing ones keep those of `defaults` (decisions E-415, E-470).
[[nodiscard]] QcVillageGolemSettings readGolemFields(const QcVillageGolemSettings &defaults, const QString &packNamespace,
                                                     QcJsonFieldReader &nested)
{
    QcVillageGolemSettings golem = defaults;
    golem.mob = nested.name(u"mob"_s, packNamespace, golem.mob);
    golem.minVillagers = nested.integer(u"minVillagers"_s, golem.minVillagers, 1, maxGolemVillagers);
    golem.gossipVillagers = nested.integer(u"gossipVillagers"_s, golem.gossipVillagers, 1, maxGolemVillagers);
    golem.gatherDistance = nested.number(u"gatherDistance"_s, golem.gatherDistance, 1.0, maxGolemDistance);
    golem.golemDistance = nested.number(u"golemDistance"_s, golem.golemDistance, 0.0, maxGolemDistance);
    golem.panicSeconds = nested.number(u"panicSeconds"_s, golem.panicSeconds, 0.0, maxGolemSeconds);
    golem.sleptSeconds = nested.number(u"sleptSeconds"_s, golem.sleptSeconds, 0.0, maxGolemSeconds);
    golem.gossipChance = nested.number(u"gossipChance"_s, golem.gossipChance, 0.0, 1.0);
    golem.cooldownSeconds = nested.number(u"cooldownSeconds"_s, golem.cooldownSeconds, 0.0, maxGolemSeconds);
    golem.spawnRadius = nested.integer(u"spawnRadius"_s, golem.spawnRadius, 2, 32);
    golem.grudgeSeconds = nested.number(u"grudgeSeconds"_s, golem.grudgeSeconds, 0.0, maxGolemSeconds);
    return golem;
}

// The fields of a style's "golem" object that it gives (decision E-421): each over the settings'; the
// fields it does not give keep those of `base`.
[[nodiscard]] QcVillageGolemPatch readGolemPatch(const QJsonObject &object, const QcVillageGolemPatch &base, const QString &packNamespace,
                                                 QcJsonFieldReader &nested)
{
    const QcVillageGolemSettings defaults;
    QcVillageGolemPatch patch = base;
    const auto number = [&](const QString &key, std::optional<double> &field, double fallback, double low, double high) {
        if (object.contains(key)) {
            field = nested.number(key, fallback, low, high);
        }
    };
    if (object.contains(u"mob"_s)) {
        patch.mob = nested.name(u"mob"_s, packNamespace);
    }
    if (object.contains(u"minVillagers"_s)) {
        patch.minVillagers = nested.integer(u"minVillagers"_s, defaults.minVillagers, 1, maxGolemVillagers);
    }
    if (object.contains(u"gossipVillagers"_s)) {
        patch.gossipVillagers = nested.integer(u"gossipVillagers"_s, defaults.gossipVillagers, 1, maxGolemVillagers);
    }
    if (object.contains(u"spawnRadius"_s)) {
        patch.spawnRadius = nested.integer(u"spawnRadius"_s, defaults.spawnRadius, 2, 32);
    }
    number(u"gatherDistance"_s, patch.gatherDistance, defaults.gatherDistance, 1.0, maxGolemDistance);
    number(u"golemDistance"_s, patch.golemDistance, defaults.golemDistance, 0.0, maxGolemDistance);
    number(u"panicSeconds"_s, patch.panicSeconds, defaults.panicSeconds, 0.0, maxGolemSeconds);
    number(u"sleptSeconds"_s, patch.sleptSeconds, defaults.sleptSeconds, 0.0, maxGolemSeconds);
    number(u"gossipChance"_s, patch.gossipChance, defaults.gossipChance, 0.0, 1.0);
    number(u"cooldownSeconds"_s, patch.cooldownSeconds, defaults.cooldownSeconds, 0.0, maxGolemSeconds);
    number(u"grudgeSeconds"_s, patch.grudgeSeconds, defaults.grudgeSeconds, 0.0, maxGolemSeconds);
    return patch;
}

// A style's own golems (decision E-421): false for none, or an object of the fields it gives over
// `golem` (the base style's when extending; the settings' golems fill in the rest).
std::optional<QcVillageGolemPatch> readStyleGolem(const QJsonObject &json, const QString &packNamespace,
                                                  std::optional<QcVillageGolemPatch> golem, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(u"golem"_s);
    if (value.isBool() && !value.toBool()) {
        QcVillageGolemPatch none;
        none.mob = QString();
        return none;
    }
    if (!value.isObject()) {
        reader.fail(u"'golem' must be false or an object"_s);
        return golem;
    }

    QcVillageGolemPatch patch;
    reader.nestedObject(u"golem"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        patch = readGolemPatch(object, golem.value_or(QcVillageGolemPatch{}), packNamespace, nested);
    });
    return patch;
}

// The other creatures of a style's villages: [{"mob", "min", "max"}].
QList<QcVillageResident> readResidents(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QList<QcVillageResident> residents;
    const QJsonArray array = json.value(u"residents"_s).toArray();
    if (!json.value(u"residents"_s).isArray() || array.size() > QcVillageStyleDefinition::maxResidents) {
        reader.fail(u"'residents' must be an array of at most %1 entries"_s.arg(QcVillageStyleDefinition::maxResidents));
        return residents;
    }
    for (const auto &value : array) {
        QcJsonFieldReader nested{value.toObject(), reader.context + u" residents"_s, {}};
        QcVillageResident resident;
        resident.mob = nested.name(u"mob"_s, packNamespace);
        resident.min = nested.integer(u"min"_s, 1, 0, 16);
        resident.max = nested.integer(u"max"_s, resident.min, 0, 16);
        if (!value.isObject() || resident.mob.isEmpty() || resident.max < resident.min) {
            nested.fail(u"every resident needs a 'mob' and 'min' <= 'max'"_s);
        }
        reader.adopt(nested);
        residents.append(resident);
    }
    return residents;
}

// Whether a style reads `key`: always for a new style, for an extension only when it is given.
bool reads(const QJsonObject &json, const QString &key, bool extending)
{
    return !extending || json.contains(key);
}

// Where a style stands and what its roads and fields are made of (all fields for a new style,
// only the given ones for an extension, which keeps the others of its base).
void StyleReader::readPlacement()
{
    if (reads(m_json, u"biomes"_s, m_extending)) {
        m_style.biomes = readBiomes(m_json, m_namespace, m_reader);
    }
    m_style.fallback = m_reader.boolean(u"fallback"_s, m_extending && m_style.fallback);
    m_style.centre = m_reader.name(u"centre"_s, m_namespace, m_extending ? m_style.centre : QString());
    m_style.lamp = m_reader.name(u"lamp"_s, m_namespace, m_extending ? m_style.lamp : QString());
    m_reader.nestedObject(u"rareCentre"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_style.rareCentre = nested.name(u"structure"_s, m_namespace);
        m_style.rareCentreChance = nested.number(u"chance"_s, 0.25, 0.0, 1.0);
        if (m_style.rareCentre.isEmpty()) {
            nested.fail(u"needs a 'structure'"_s);
        }
    });
    if (reads(m_json, u"path"_s, m_extending)) {
        m_style.roads = readWeightedList<QcVillageRoad>(m_json, u"path"_s, m_namespace, QcVillageStyleDefinition::maxRoads, m_reader);
        if (m_style.roads.isEmpty()) {
            m_style.roads.append({u"qtcraft:gravel"_s, {}, 1});
        }
    }
    if (reads(m_json, u"branchPath"_s, m_extending)) {
        m_style.branchRoads = readWeightedList<QcVillageRoad>(m_json, u"branchPath"_s, m_namespace, QcVillageStyleDefinition::maxRoads, m_reader);
        if (m_style.branchRoads.isEmpty()) {
            m_style.branchRoads = m_style.roads;
        }
    }
    if (reads(m_json, u"crops"_s, m_extending)) {
        m_style.crops = readWeightedList<QcVillageCrop>(m_json, u"crops"_s, m_namespace, QcVillageStyleDefinition::maxCrops, m_reader);
    }
    m_style.foundationBlock = m_reader.name(u"foundation"_s, m_namespace, m_extending ? m_style.foundationBlock : u"cobblestone"_s);
}

// What a style builds and who lives there (as readPlacement for an extension).
void StyleReader::readBuildings()
{
    if (reads(m_json, u"pieces"_s, m_extending)) {
        m_style.pieces = readPieces(m_json, m_namespace, m_reader);
    }
    if (reads(m_json, u"layouts"_s, m_extending)) {
        m_style.layouts = QcVillageLayoutParser::read(m_json, m_reader);
    }
    // Ground, stilts and wetland are read together.
    if (!m_extending || m_json.contains(u"ground"_s) || m_json.contains(u"stilts"_s) || m_json.contains(u"wetland"_s)) {
        readGround();
    }
    if (reads(m_json, u"palette"_s, m_extending)) {
        m_style.palette = nameMap(m_json, u"palette"_s, m_namespace, QcVillageStyleDefinition::maxPaletteEntries, m_reader);
    }
    if (reads(m_json, u"look"_s, m_extending)) {
        m_style.look = QcVillagerParser::readLook(u"look"_s, m_namespace, m_reader);
    }
    m_style.villager = m_reader.name(u"villager"_s, m_namespace, m_extending ? m_style.villager : QString());
    if (m_json.contains(u"golem"_s)) {
        m_style.golem = readStyleGolem(m_json, m_namespace, m_extending ? m_style.golem : std::nullopt, m_reader);
    }
    if (m_json.contains(u"residents"_s)) {
        m_style.residents = readResidents(m_json, m_namespace, m_reader);
    }
}

// The lists of `add` appended to the style's (decision E-421): biomes (once each), crops, pieces
// and layouts, each within its limit.
void StyleReader::readAdditions()
{
    m_reader.nestedObject(u"add"_s, [this](const QJsonObject &add, QcJsonFieldReader &nested) {
        const QStringList biomes = readBiomes(add, m_namespace, nested);
        for (const QString &biome : biomes) {
            if (!m_style.biomes.contains(biome)) {
                m_style.biomes.append(biome);
            }
        }
        if (add.contains(u"crops"_s)) {
            m_style.crops += readWeightedList<QcVillageCrop>(add, u"crops"_s, m_namespace, QcVillageStyleDefinition::maxCrops, nested);
        }
        if (add.contains(u"pieces"_s)) {
            m_style.pieces += readPieces(add, m_namespace, nested);
        }
        if (add.contains(u"layouts"_s)) {
            m_style.layouts += QcVillageLayoutParser::read(add, nested);
        }
        if (m_style.biomes.size() > QcVillageStyleDefinition::maxBiomes || m_style.crops.size() > QcVillageStyleDefinition::maxCrops
            || m_style.pieces.size() > QcVillageStyleDefinition::maxPieces || m_style.layouts.size() > QcVillageStyleDefinition::maxLayouts) {
            nested.fail(u"the style holds too many biomes, crops, pieces or layouts with these"_s);
        }
    });
}

// The lists of `remove` taken from the style's (decision E-421): biomes, so a mod moves a biome of
// a base style to its own.
void StyleReader::readRemovals()
{
    m_reader.nestedObject(u"remove"_s, [this](const QJsonObject &remove, QcJsonFieldReader &nested) {
        const QStringList biomes = readBiomes(remove, m_namespace, nested);
        for (const QString &biome : biomes) {
            m_style.biomes.removeAll(biome);
        }
    });
}

QcResult<QcVillageStyleDefinition> StyleReader::read()
{
    QcContentFields::checkFormatVersion(QcContentFields::villageFormatVersion, m_reader);
    if (m_json.contains(u"villagers"_s)) {
        m_reader.fail(u"'villagers' (the look of the villagers) is now 'look'"_s);
    }
    readPlacement();
    readBuildings();
    if (m_extending) {
        readRemovals();
        readAdditions();
    } else if (m_json.contains(u"add"_s) || m_json.contains(u"remove"_s)) {
        m_reader.fail(u"'add' and 'remove' need 'extends'"_s);
    }
    if (m_reader.ok() && m_style.biomes.size() > QcVillageStyleDefinition::maxBiomes) {
        m_reader.fail(u"'biomes' lists at most %1 biomes"_s.arg(QcVillageStyleDefinition::maxBiomes));
    }
    if (m_style.centre.isEmpty()) {
        m_reader.fail(u"'centre' is required"_s);
    }
    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return std::move(m_style);
}

} // namespace

namespace QcVillageParser {

QcResult<QcVillageStyleDefinition> parseVillageStyle(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"village style"_s, {}};
    QcVillageStyleDefinition style;
    style.name = reader.name(u"name"_s, packNamespace);
    if (style.name.isEmpty()) {
        return qcError(reader.ok() ? u"village style: 'name' is required"_s : reader.error);
    }
    if (json.contains(u"extends"_s)) {
        return qcError(u"village style '%1': 'extends' is read with the style it extends"_s.arg(style.name));
    }
    return StyleReader(std::move(style), json, packNamespace, false).read();
}

QcResult<QString> extendedStyle(const QJsonObject &json, const QString &packNamespace)
{
    const QJsonValue value = json.value(u"extends"_s);
    QString name = value.isString() ? QcContentNames::qualify(value.toString(), packNamespace) : QString();
    if (name.isEmpty()) {
        return qcError(u"village style: 'extends' must name a village style"_s);
    }
    return name;
}

QcResult<QcVillageStyleDefinition> extendVillageStyle(const QJsonObject &json, const QString &packNamespace,
                                                      const QcVillageStyleDefinition &base)
{
    QcJsonFieldReader reader{json, u"village style"_s, {}};
    QcVillageStyleDefinition style = base;
    style.name = reader.name(u"name"_s, packNamespace, base.name);
    if (!reader.ok()) {
        return qcError(u"village style extending '%1': %2"_s.arg(base.name, reader.error));
    }
    // A new style from a copy is built where it says, never in the base's biomes.
    if (style.name != base.name) {
        style.biomes.clear();
        style.fallback = false;
    }
    return StyleReader(std::move(style), json, packNamespace, true).read();
}

QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace)
{
    return parseVillageSettings(json, packNamespace, QcVillageSettings{});
}

QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace, QcVillageSettings settings)
{
    QcJsonFieldReader reader{json, u"village settings"_s, {}};
    QcContentFields::checkFormatVersion(QcContentFields::villageFormatVersion, reader);
    if (json.contains(u"workstations"_s)) {
        reader.fail(u"'workstations' moved to the professions (professions/<name>.json)"_s);
    }
    settings.spacing = reader.integer(u"spacing"_s, settings.spacing, 128, 4096);
    settings.chance = reader.number(u"chance"_s, settings.chance, 0.0, 1.0);
    settings.attempts = reader.integer(u"attempts"_s, settings.attempts, 1, 32);
    // A village stays inside its region: the radius leaves a margin on both sides.
    settings.radius = reader.integer(u"radius"_s, settings.radius, 16, settings.spacing / 2 - 16);
    settings.maxHeightSpread = reader.integer(u"maxHeightSpread"_s, settings.maxHeightSpread, 0, 64);
    settings.maxPieceSlope = reader.integer(u"maxPieceSlope"_s, settings.maxPieceSlope, 0, 16);
    settings.lampSpacing = reader.integer(u"lampSpacing"_s, settings.lampSpacing, 4, 64);
    settings.bell = reader.name(u"bell"_s, packNamespace, settings.bell);
    settings.villager = reader.name(u"villager"_s, packNamespace, settings.villager);
    reader.nestedObject(u"golem"_s, [&settings, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        settings.golem = readGolemFields(settings.golem, packNamespace, nested);
    });
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

} // namespace QcVillageParser
