#include "common/content/parser/QcStructureParser.h"

#include "common/content/definitions/QcMobTraits.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcStructureSiteParser.h"
#include "common/content/parser/QcStructureVariationParser.h"
#include "common/content/registry/QcContentNames.h"
#include "common/world/QcSignText.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Structure = QcStructureDefinition;

// Labels and creatures may sit a little outside the blocks (above a roof).
constexpr double positionMargin = 4.0;

[[nodiscard]] std::optional<QcVec3> readPoint(const QJsonValue &value, const Structure &structure)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 3 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble()) {
        return std::nullopt;
    }

    const QcVec3 point{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
    const auto inside = [](double v, int size) { return std::isfinite(v) && v >= -positionMargin && v <= size + positionMargin; };
    if (!inside(point.x, structure.sizeX) || !inside(point.y, structure.sizeY) || !inside(point.z, structure.sizeZ)) {
        return std::nullopt;
    }
    return point;
}

// A cell inside the structure.
[[nodiscard]] std::optional<QcBlockPos> readCell(const QJsonValue &value, const Structure &structure)
{
    const std::optional<QcVec3> point = readPoint(value, structure);
    if (!point || point->x != std::floor(point->x) || point->y != std::floor(point->y) || point->z != std::floor(point->z)) {
        return std::nullopt;
    }

    const QcBlockPos cell{static_cast<int>(point->x), static_cast<int>(point->y), static_cast<int>(point->z)};
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= structure.sizeX || cell.y >= structure.sizeY || cell.z >= structure.sizeZ) {
        return std::nullopt;
    }
    return cell;
}

// An optional array of at most maxExtras entries; fails the reader otherwise.
QJsonArray extraArray(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(key);
    if (!value.isUndefined() && (!value.isArray() || value.toArray().size() > Structure::maxExtras)) {
        reader.fail(u"'%1' must be an array of at most %2 entries"_s.arg(key).arg(Structure::maxExtras));
        return {};
    }
    return value.toArray();
}

// The cells listed under `key`; on an invalid entry the reader fails and the cells read so far come back.
[[nodiscard]] QList<QcBlockPos> parseCells(const QJsonObject &json, const QString &key, const Structure &structure, QcJsonFieldReader &reader)
{
    QList<QcBlockPos> cells;
    const QJsonArray entries = extraArray(json, key, reader);
    for (const auto &entry : entries) {
        const std::optional<QcBlockPos> cell = readCell(entry, structure);
        if (!cell) {
            reader.fail(u"'%1' entries must be [x, y, z] cells inside the structure"_s.arg(key));
            return cells;
        }
        cells.append(*cell);
    }
    return cells;
}

// Reads one structure: owns the definition being built, the reader of its JSON object and the keys of
// its palette. The palette and the layers come first, the extras are checked against the size.
class StructureReader
{
public:
    StructureReader(const QJsonObject &json, const QString &packNamespace)
        : m_json(json)
        , m_namespace(packNamespace)
        , m_reader{json, u"structure"_s, {}}
    {
    }

    [[nodiscard]] QcResult<QcStructureDefinition> read();

private:
    void parsePalette();
    bool measureLayers(const QJsonArray &layers);
    bool parseRow(const QString &row, QcBlockPos start);
    void parseLayers();
    void parseMobs();
    void parseStands();
    void parseContents();
    void parseLabels();
    void parseCarts();
    void parseSpawnersAndLoot();
    void parseSigns();
    void parseHangings();
    void parseAdventure();
    void parseExtras();

    const QJsonObject &m_json;
    const QString &m_namespace;
    QcJsonFieldReader m_reader;
    QHash<QChar, qint16> m_keys;
    Structure m_structure;
};

void StructureReader::parsePalette()
{
    const QJsonObject palette = m_json.value(u"palette"_s).toObject();
    if (!m_json.value(u"palette"_s).isObject() || palette.isEmpty() || palette.size() > Structure::maxPaletteEntries) {
        m_reader.fail(u"'palette' must be an object with 1 to %1 entries"_s.arg(Structure::maxPaletteEntries));
        return;
    }

    for (auto it = palette.constBegin(); it != palette.constEnd(); ++it) {
        const QString state = it.value().isString() ? QcStructureParser::qualifiedStateName(it.value().toString(), m_namespace) : QString();
        if (it.key().size() != 1 || it.key() == u" "_s || state.isEmpty()) {
            m_reader.fail(u"palette entry '%1' needs a single character other than space and a block name"_s.arg(it.key()));
            return;
        }
        m_keys.insert(it.key().front(), static_cast<qint16>(m_structure.palette.size()));
        m_structure.palette.append(state);
    }
}

// The size of the layers: every layer has the same rows, every row the same length.
bool StructureReader::measureLayers(const QJsonArray &layers)
{
    if (layers.isEmpty() || layers.size() > Structure::maxSize || !layers[0].isArray()) {
        return false;
    }

    m_structure.sizeY = static_cast<int>(layers.size());
    m_structure.sizeZ = static_cast<int>(layers[0].toArray().size());
    m_structure.sizeX = m_structure.sizeZ > 0 ? static_cast<int>(layers[0].toArray()[0].toString().size()) : 0;
    if (m_structure.sizeZ < 1 || m_structure.sizeZ > Structure::maxSize || m_structure.sizeX < 1 || m_structure.sizeX > Structure::maxSize) {
        return false;
    }

    for (const auto &layer : layers) {
        const QJsonArray rows = layer.toArray();
        if (!layer.isArray() || rows.size() != m_structure.sizeZ) {
            return false;
        }
        for (const auto &row : rows) {
            if (!row.isString() || row.toString().size() != m_structure.sizeX) {
                return false;
            }
        }
    }
    return true;
}

// One row of a layer (`start` is its first cell); a space keeps the cell.
bool StructureReader::parseRow(const QString &row, QcBlockPos start)
{
    for (int x = 0; x < m_structure.sizeX; ++x) {
        if (row[x] == u' ') {
            continue;
        }
        if (!m_keys.contains(row[x])) {
            m_reader.fail(u"layer %1 uses '%2', which the palette does not define"_s.arg(start.y).arg(row[x]));
            return false;
        }
        m_structure.cells[m_structure.cellIndex(x, start.y, start.z)] = m_keys.value(row[x]);
    }
    return true;
}

void StructureReader::parseLayers()
{
    const QJsonArray layers = m_json.value(u"layers"_s).toArray();
    if (!measureLayers(layers)) {
        m_reader.fail(u"'layers' must be 1 to %1 layers of equally many rows of equal length (at most %1 each)"_s.arg(Structure::maxSize));
        return;
    }

    m_structure.cells.resize(static_cast<qsizetype>(m_structure.sizeX) * m_structure.sizeY * m_structure.sizeZ, Structure::keep);
    for (int y = 0; y < m_structure.sizeY; ++y) {
        const QJsonArray rows = layers[y].toArray();
        for (int z = 0; z < m_structure.sizeZ; ++z) {
            if (!parseRow(rows[z].toString(), {0, y, z})) {
                return;
            }
        }
    }
}

void StructureReader::parseMobs()
{
    const QJsonArray mobs = extraArray(m_json, u"mobs"_s, m_reader);
    for (const auto &entry : mobs) {
        const QJsonObject object = entry.toObject();
        const QString mob = QcContentNames::qualify(object.value(u"mob"_s).toString(), m_namespace);
        const std::optional<QcVec3> at = readPoint(object.value(u"at"_s), m_structure);
        if (mob.isEmpty() || !at) {
            m_reader.fail(u"'mobs' entries need 'mob' and 'at' inside the structure"_s);
            return;
        }
        QcJsonFieldReader entryReader{object, m_reader.context + u" mobs"_s, {}};
        const bool confined = entryReader.boolean(u"confined"_s, false);
        const QString owner = entryReader.string(u"owner"_s);
        const bool sitting = entryReader.boolean(u"sitting"_s, true);
        const bool noAi = entryReader.boolean(u"noAi"_s, false);
        const int children = entryReader.integer(u"children"_s, 0, 0, QcFamilyDefinition::maxChildrenLimit);
        m_reader.adopt(entryReader);
        if (!m_reader.ok()) {
            return;
        }
        m_structure.mobs.append({mob, *at, confined, owner, sitting, noAi, children});
    }
}

void StructureReader::parseStands()
{
    constexpr qsizetype maxProperties = 32;
    const QJsonArray stands = extraArray(m_json, u"stands"_s, m_reader);
    for (const auto &entry : stands) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcVec3> at = readPoint(object.value(u"at"_s), m_structure);
        const double yaw = object.value(u"yaw"_s).toDouble(0.0);
        const QJsonArray properties = object.value(u"properties"_s).toArray();
        if (!at || !std::isfinite(yaw) || properties.size() > maxProperties) {
            m_reader.fail(u"'stands' entries need 'at' inside the structure, a 'yaw' and at most %1 'properties'"_s.arg(maxProperties));
            return;
        }

        Structure::Stand stand{*at, static_cast<float>(yaw), {}};
        for (const auto &property : properties) {
            stand.properties.append(property.toString().left(256));
        }
        m_structure.stands.append(stand);
    }
}

[[nodiscard]] std::optional<Structure::Stack> readStack(const QJsonValue &value, const QString &packNamespace)
{
    const QJsonObject object = value.toObject();
    const QString item = QcContentNames::qualify(object.value(u"item"_s).toString(), packNamespace);
    const QJsonValue count = object.value(u"count"_s);
    const int amount = count.isUndefined() ? 1 : count.toInt(0);
    if (!value.isObject() || item.isEmpty() || amount < 1 || amount > 64) {
        return std::nullopt;
    }
    return Structure::Stack{item, amount};
}

void StructureReader::parseContents()
{
    const QJsonArray contents = extraArray(m_json, u"contents"_s, m_reader);
    for (const auto &entry : contents) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> at = readCell(object.value(u"at"_s), m_structure);
        const QJsonArray items = object.value(u"items"_s).toArray();
        if (!at || items.size() > Structure::maxItemsPerContainer) {
            m_reader.fail(u"'contents' entries need 'at' inside the structure and at most %1 'items'"_s.arg(Structure::maxItemsPerContainer));
            return;
        }

        Structure::Contents placed{*at, {}};
        for (const auto &item : items) {
            const std::optional<Structure::Stack> stack = readStack(item, m_namespace);
            if (!stack) {
                m_reader.fail(u"'items' entries need 'item' and a 'count' of 1 to 64"_s);
                return;
            }
            placed.items.append(*stack);
        }
        m_structure.contents.append(placed);
    }
}

void StructureReader::parseLabels()
{
    const QJsonArray labels = extraArray(m_json, u"labels"_s, m_reader);
    for (const auto &entry : labels) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcVec3> at = readPoint(object.value(u"at"_s), m_structure);
        const QString text = object.value(u"text"_s).toString();
        const QString detail = object.value(u"detail"_s).toString();
        if (!at || text.isEmpty() || text.size() > 256 || detail.size() > 256) {
            m_reader.fail(u"'labels' entries need 'at' inside the structure and a 'text' (at most 256 characters)"_s);
            return;
        }
        m_structure.labels.append({*at, text, detail, object.value(u"heading"_s).toBool(false)});
    }
}

// "minecarts": [x, y, z] for a plain cart or {"at": [x, y, z], "item": "chest_minecart"}.
void StructureReader::parseCarts()
{
    const QJsonArray entries = extraArray(m_json, u"minecarts"_s, m_reader);
    for (const auto &entry : entries) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> cell = readCell(entry.isObject() ? object.value(u"at"_s) : entry, m_structure);
        const QString item = QcContentNames::qualify(entry.isObject() ? object.value(u"item"_s).toString(u"qtcraft:minecart"_s) : u"qtcraft:minecart"_s,
                                                     m_namespace);
        if (!cell || item.isEmpty()) {
            m_reader.fail(u"'minecarts' entries must be [x, y, z] cells inside the structure or objects with 'at' and 'item'"_s);
            return;
        }
        m_structure.minecarts.append({*cell, item});
    }
}

// "spawners": {"at": [x, y, z], "mob": "zombie", "confined": true}; "loot": {"at": [x, y, z], "table":
// "dungeon_chest"} (decisions E-520, E-521).
void StructureReader::parseSpawnersAndLoot()
{
    const QJsonArray spawners = extraArray(m_json, u"spawners"_s, m_reader);
    for (const auto &entry : spawners) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> cell = readCell(object.value(u"at"_s), m_structure);
        const QString mob = QcContentNames::qualify(object.value(u"mob"_s).toString(), m_namespace);
        if (!cell || object.value(u"mob"_s).toString().isEmpty()) {
            m_reader.fail(u"'spawners' entries need 'at' inside the structure and a 'mob'"_s);
            return;
        }
        m_structure.spawners.append({*cell, mob, object.value(u"confined"_s).toBool(false)});
    }
    const QJsonArray loot = extraArray(m_json, u"loot"_s, m_reader);
    for (const auto &entry : loot) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> cell = readCell(object.value(u"at"_s), m_structure);
        if (!cell || object.value(u"table"_s).toString().isEmpty()) {
            m_reader.fail(u"'loot' entries need 'at' inside the structure and a 'table'"_s);
            return;
        }
        m_structure.loot.append({*cell, QcContentNames::qualify(object.value(u"table"_s).toString(), m_namespace)});
    }
}

// "signs": {"at": [x, y, z], "lines": ["...", ...], "color": "blue"}: the text of a sign (decision
// E-766), at most four lines of 32 characters (cleaned like a player's when the world is made), and
// optionally its colour, one of the 16 dye colours (decision E-830).
void StructureReader::parseSigns()
{
    const QJsonArray signs = extraArray(m_json, u"signs"_s, m_reader);
    for (const auto &entry : signs) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> cell = readCell(object.value(u"at"_s), m_structure);
        const QJsonArray lines = object.value(u"lines"_s).toArray();
        const bool fits = lines.size() <= QcSignText::maxLines && std::ranges::all_of(lines, [](const QJsonValue &line) {
                              return line.isString() && line.toString().size() <= QcSignText::maxLineLength;
                          });
        if (!cell || !fits) {
            m_reader.fail(u"'signs' entries need 'at' inside the structure and 'lines': at most 4 strings of 32 characters"_s);
            return;
        }
        const QJsonValue colorValue = object.value(u"color"_s);
        const std::optional<QString> color = colorValue.isUndefined() ? std::optional(QString())
                                             : colorValue.isString()  ? QcSignText::colorOfDye(colorValue.toString())
                                                                      : std::nullopt;
        if (!color) {
            m_reader.fail(u"'signs' entry 'color' must be one of the 16 dye colours, not %1"_s.arg(
                QString::fromUtf8(QJsonDocument(QJsonArray{colorValue}).toJson(QJsonDocument::Compact))));
            return;
        }
        QStringList text;
        for (const auto &line : lines) {
            text.append(line.toString());
        }
        m_structure.signs.append({*cell, text, *color});
    }
}

// "hangings": {"at": [x, y, z], "face": "north", "item": "item_frame", "held": "apple", "rotation":
// 0 to 7} or {..., "item": "painting", "motif": "valley"}: item frames and paintings hung when the
// world is made (decision E-797); the face is one of the block faces' names. A frame holding a
// filled map may add "map": [east, south], which map of a wall map it shows (decision E-811). Only
// the showcase hangs them (QcWorldSetup), so a structure without "showcase" may not have them.
void StructureReader::parseHangings()
{
    static constexpr std::array<std::pair<QStringView, QcBlockFace>, 6> faces = {{{u"east", QcBlockFace::East},
                                                                                   {u"west", QcBlockFace::West},
                                                                                   {u"up", QcBlockFace::Up},
                                                                                   {u"down", QcBlockFace::Down},
                                                                                   {u"south", QcBlockFace::South},
                                                                                   {u"north", QcBlockFace::North}}};
    if (m_json.contains(u"hangings"_s) && !m_structure.showcase) {
        m_reader.fail(u"only showcase structures may have 'hangings'"_s);
        return;
    }
    const QJsonArray entries = extraArray(m_json, u"hangings"_s, m_reader);
    for (const auto &entry : entries) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> cell = readCell(object.value(u"at"_s), m_structure);
        const QString faceName = object.value(u"face"_s).toString();
        const auto face = std::ranges::find(faces, QStringView(faceName), &std::pair<QStringView, QcBlockFace>::first);
        const int rotation = object.value(u"rotation"_s).toInt(0);
        if (!cell || face == faces.end() || object.value(u"item"_s).toString().isEmpty() || rotation < 0 || rotation > 7) {
            m_reader.fail(u"'hangings' entries need 'at' inside the structure, a 'face', an 'item' and a 'rotation' of 0 to 7"_s);
            return;
        }
        const QString held = object.value(u"held"_s).toString();
        std::optional<QPoint> map;
        if (object.contains(u"map"_s)) {
            const QJsonArray offset = object.value(u"map"_s).toArray();
            const int east = offset.at(0).toInt(-1);
            const int south = offset.at(1).toInt(-1);
            if (offset.size() != 2 || east < 0 || east > 3 || south < 0 || south > 3) {
                m_reader.fail(u"'map' of a hanging is [east, south], each 0 to 3"_s);
                return;
            }
            if (held.isEmpty() || object.contains(u"motif"_s)) {
                m_reader.fail(u"'map' belongs to an item frame with a filled map as 'held', not to a painting"_s);
                return;
            }
            map = QPoint(east, south);
        }
        m_structure.hangings.append({*cell, face->second, QcContentNames::qualify(object.value(u"item"_s).toString(), m_namespace),
                                     held.isEmpty() ? QString() : QcContentNames::qualify(held, m_namespace), rotation,
                                     object.value(u"motif"_s).toString(), map});
    }
}

// "adventure": the allow-list entries of adventure mode (decision E-735), names qualified like the
// palette's; "*" stays the wildcard. Only the showcase brings them into a world, so a structure
// without "showcase" may not have them; the names are checked against the content when it is
// finished (QcContentRegistry).
void StructureReader::parseAdventure()
{
    const QJsonValue value = m_json.value(u"adventure"_s);
    if (value.isUndefined()) {
        return;
    }
    if (!value.isObject() || !m_structure.showcase) {
        m_reader.fail(u"'adventure' must be an object, and only showcase structures may have it"_s);
        return;
    }

    const QcAdventureRules::Read written = QcAdventureRules::fromJson(value.toObject());
    if (!written.problems.isEmpty()) {
        m_reader.fail(u"'adventure': "_s + written.problems.constFirst());
        return;
    }

    const auto qualify = [this](const QString &name) {
        return name == QLatin1StringView(QcAdventureRules::anyItem) ? name : QcContentNames::qualify(name, m_namespace);
    };
    for (const QcAdventureRules::Action action : {QcAdventureRules::Action::Break, QcAdventureRules::Action::PlaceOn}) {
        const QStringList items = written.rules.items(action);
        for (const QString &item : items) {
            QStringList blocks = written.rules.blocks(action, item);
            std::ranges::transform(blocks, blocks.begin(), qualify);
            if (!m_structure.adventure.allow(action, qualify(item), blocks)) {
                m_reader.fail(u"'adventure' lists are too long"_s);
                return;
            }
        }
    }
}

// The creatures, stands, contents, labels, cells, carts, spawners, variation and site, all within the size.
void StructureReader::parseExtras()
{
    parseAdventure();
    parseMobs();
    parseStands();
    parseContents();
    parseLabels();
    m_structure.updates += parseCells(m_json, u"updates"_s, m_structure, m_reader);
    m_structure.growing += parseCells(m_json, u"growing"_s, m_structure, m_reader);
    parseCarts();
    parseSpawnersAndLoot();
    parseSigns();
    parseHangings();
    m_structure.choices = QcStructureVariationParser::parseChoices(m_json, m_reader, m_namespace);
    if (m_reader.ok()) {
        m_structure.parts = QcStructureVariationParser::parseParts(m_json, m_keys, m_structure, m_reader);
    }

    QcStructureSiteParser::Site site = QcStructureSiteParser::parseSite(m_json, m_structure, m_reader, m_namespace);
    m_structure.groundLayer = site.groundLayer;
    m_structure.surface = std::move(site.surface);
    m_structure.guardians = std::move(site.guardians);
    m_structure.spawnArea = std::move(site.spawnArea);
}

QcResult<QcStructureDefinition> StructureReader::read()
{
    m_structure.name = m_reader.name(u"name"_s, m_namespace);
    if (m_structure.name.isEmpty()) {
        return qcError(m_reader.ok() ? u"structure: 'name' is required"_s : m_reader.error);
    }

    m_reader.context = u"structure '%1'"_s.arg(m_structure.name);
    m_structure.translationKey = m_reader.string(u"translation"_s, QcContentFields::translationKey(u"structure"_s, m_structure.name));
    m_reader.nestedObject(u"showcase"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_structure.showcase = true;
        m_structure.showcaseOrder = nested.integer(u"order"_s, 0, -10000, 10000);
        m_structure.showcaseFromFront = nested.boolean(u"front"_s, false);
    });
    m_reader.nestedObject(u"mineshaft"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_structure.mineshaftWeight = nested.integer(u"weight"_s, 1, 1, 1000);
    });

    parsePalette();
    if (m_reader.ok()) {
        parseLayers();
    }
    if (m_reader.ok()) {
        parseExtras();
    }

    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return std::move(m_structure);
}

} // namespace

namespace QcStructureParser {

QString qualifiedStateName(const QString &text, const QString &packNamespace)
{
    const qsizetype bracket = text.indexOf(u'[');
    const QString name = QcContentNames::qualify(bracket < 0 ? text : text.left(bracket), packNamespace);
    if (name.isEmpty() || (bracket >= 0 && !text.endsWith(u']'))) {
        return {};
    }
    return bracket < 0 ? name : name + text.mid(bracket);
}

[[nodiscard]] QcResult<QcStructureDefinition> parseStructure(const QJsonObject &json, const QString &packNamespace)
{
    return StructureReader(json, packNamespace).read();
}

} // namespace QcStructureParser
