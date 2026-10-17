#include "common/content/parser/QcVillageParser.h"

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

// The rules of a kind (decision E-404, E-408): chance, minRoadLength, water with its jetty (oak by
// default).
void readRules(QcVillagePiece &piece, const QString &packNamespace, QcJsonFieldReader &reader)
{
    piece.chance = reader.number(u"chance"_s, 1.0, 0.0, 1.0);
    piece.minRoadLength = reader.integer(u"minRoadLength"_s, 0, 0, 4096);
    piece.water = reader.integer(u"water"_s, 0, 0, 32);
    piece.jetty.deck = u"qtcraft:oak_planks"_s;
    piece.jetty.post = u"qtcraft:oak_log"_s;
    reader.nestedObject(u"jetty"_s, [&piece, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        piece.jetty.deck = nested.name(u"deck"_s, packNamespace, piece.jetty.deck);
        piece.jetty.post = nested.name(u"post"_s, packNamespace, piece.jetty.post);
        piece.jetty.width = nested.integer(u"width"_s, 1, 1, 5) | 1;
        piece.jetty.over = nested.integer(u"over"_s, 4, 1, 16);
    });
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
        readRules(piece, packNamespace, nested);
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

// Where the style stands (decision E-407): "ground" "land" (default) or "water", the latter with its
// "stilts" (posts and stairs of spruce by default) and an optional "wetland" (the ground it shapes).
void readGround(QcVillageStyleDefinition &style, const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QString ground = reader.string(u"ground"_s, u"land"_s);
    if (ground != u"land"_s && ground != u"water"_s) {
        reader.fail(u"'ground' must be \"land\" or \"water\""_s);
        return;
    }
    style.ground = ground == u"water"_s ? QcVillageStyleDefinition::Ground::Water : QcVillageStyleDefinition::Ground::Land;

    QcVillageStilts &stilts = style.stilts;
    stilts.post = u"qtcraft:spruce_log"_s;
    stilts.stair = u"qtcraft:spruce_stairs"_s;
    reader.nestedObject(u"stilts"_s, [&stilts, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        stilts.height = nested.integer(u"height"_s, stilts.height, 2, 8);
        stilts.spacing = nested.integer(u"spacing"_s, stilts.spacing, 1, 8);
        stilts.maxDepth = nested.integer(u"maxDepth"_s, stilts.maxDepth, 0, 16);
        stilts.water = nested.number(u"water"_s, stilts.water, 0.0, 1.0);
        stilts.post = nested.name(u"post"_s, packNamespace, stilts.post);
        stilts.stair = nested.name(u"stair"_s, packNamespace, stilts.stair);
    });

    QcVillageWetland &wetland = style.wetland;
    wetland.bottom = u"qtcraft:mud"_s;
    reader.nestedObject(u"wetland"_s, [&wetland, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        wetland.enabled = true;
        wetland.depth = nested.integer(u"depth"_s, wetland.depth, 1, 4);
        wetland.margin = nested.integer(u"margin"_s, wetland.margin, 0, 12);
        wetland.fray = nested.integer(u"fray"_s, wetland.fray, 0, 8);
        wetland.blend = nested.integer(u"blend"_s, wetland.blend, 1, 8);
        wetland.bottom = nested.name(u"bottom"_s, packNamespace, wetland.bottom);
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

    reader.context = u"village style '%1'"_s.arg(style.name);
    style.biomes = readBiomes(json, packNamespace, reader);
    style.fallback = reader.boolean(u"fallback"_s, false);
    style.centre = reader.name(u"centre"_s, packNamespace);
    style.lamp = reader.name(u"lamp"_s, packNamespace);
    style.roads = readWeightedList<QcVillageRoad>(json, u"path"_s, packNamespace, QcVillageStyleDefinition::maxRoads, reader);
    if (style.roads.isEmpty()) {
        style.roads.append({u"qtcraft:gravel"_s, {}, 1});
    }
    style.branchRoads = readWeightedList<QcVillageRoad>(json, u"branchPath"_s, packNamespace, QcVillageStyleDefinition::maxRoads, reader);
    if (style.branchRoads.isEmpty()) {
        style.branchRoads = style.roads;
    }
    style.crops = readWeightedList<QcVillageCrop>(json, u"crops"_s, packNamespace, QcVillageStyleDefinition::maxCrops, reader);
    style.foundationBlock = reader.name(u"foundation"_s, packNamespace, u"cobblestone"_s);
    style.pieces = readPieces(json, packNamespace, reader);
    style.layouts = QcVillageLayoutParser::read(json, reader);
    readGround(style, packNamespace, reader);
    style.palette = nameMap(json, u"palette"_s, packNamespace, QcVillageStyleDefinition::maxPaletteEntries, reader);
    style.villagers = QcVillagerParser::readLook(u"villagers"_s, packNamespace, reader);
    if (style.centre.isEmpty()) {
        reader.fail(u"'centre' is required"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return style;
}

QcResult<QcVillageSettings> parseVillageSettings(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"village settings"_s, {}};
    QcVillageSettings settings;
    settings.spacing = reader.integer(u"spacing"_s, settings.spacing, 128, 4096);
    settings.chance = reader.number(u"chance"_s, settings.chance, 0.0, 1.0);
    settings.attempts = reader.integer(u"attempts"_s, settings.attempts, 1, 32);
    // A village stays inside its region: the radius leaves a margin on both sides.
    settings.radius = reader.integer(u"radius"_s, settings.radius, 16, settings.spacing / 2 - 16);
    settings.maxHeightSpread = reader.integer(u"maxHeightSpread"_s, settings.maxHeightSpread, 0, 64);
    settings.maxPieceSlope = reader.integer(u"maxPieceSlope"_s, settings.maxPieceSlope, 0, 16);
    settings.lampSpacing = reader.integer(u"lampSpacing"_s, settings.lampSpacing, 4, 64);
    settings.bell = reader.name(u"bell"_s, packNamespace);
    settings.workstations = nameMap(json, u"workstations"_s, packNamespace, 64, reader);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

} // namespace QcVillageParser
