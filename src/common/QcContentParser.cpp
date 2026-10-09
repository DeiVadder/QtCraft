#include "QcContentParser.h"

#include "QcContentNames.h"
#include "QcSoundKeys.h"
#include "QcWorldConstants.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <array>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

struct FieldReader
{
    const QJsonObject &json;
    QString context;
    QString error;

    bool ok() const { return error.isEmpty(); }

    void fail(const QString &message)
    {
        if (error.isEmpty()) {
            error = context + u": "_s + message;
        }
    }

    QString string(const QString &key, const QString &fallback = {})
    {
        const QJsonValue value = json.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isString()) {
            fail(u"'%1' must be a string"_s.arg(key));
            return fallback;
        }
        return value.toString();
    }

    bool boolean(const QString &key, bool fallback)
    {
        const QJsonValue value = json.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isBool()) {
            fail(u"'%1' must be true or false"_s.arg(key));
            return fallback;
        }
        return value.toBool();
    }

    double number(const QString &key, double fallback, double low, double high)
    {
        const QJsonValue value = json.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isDouble() || value.toDouble() < low || value.toDouble() > high) {
            fail(u"'%1' must be a number within %2..%3"_s.arg(key).arg(low).arg(high));
            return fallback;
        }
        return value.toDouble();
    }

    int integer(const QString &key, int fallback, int low, int high)
    {
        const double value = number(key, fallback, low, high);
        if (value != double(int(value))) {
            fail(u"'%1' must be an integer"_s.arg(key));
            return fallback;
        }
        return int(value);
    }

    QString name(const QString &key, const QString &packNamespace, const QString &fallback = {})
    {
        QString raw = string(key, fallback);
        if (raw.isEmpty()) {
            return raw;
        }
        const QString qualified = QcContentNames::qualify(raw, packNamespace);
        if (qualified.isEmpty()) {
            fail(u"'%1' is not a valid name: '%2'"_s.arg(key, raw));
        }
        return qualified;
    }
};

constexpr double maxFuelSeconds = 100000.0;
constexpr double minMachineSpeed = 0.01;
constexpr double maxMachineSpeed = 100.0;
constexpr qsizetype maxCategoryLength = 64;
constexpr int maxPoolAttempts = 8;

// Categories are plain names without namespace, so that every pack can add recipes to "smelting".
bool isValidCategory(const QString &category)
{
    return category.size() <= maxCategoryLength && !category.contains(u'/') && QcContentNames::isValidPart(category);
}

QString invalidCategoryMessage()
{
    return u"'category' must be a name of lower-case letters, digits, '_', '-' and '.' (at most 64 characters)"_s;
}

std::optional<QcRenderLayer> renderLayerFromName(const QString &name)
{
    if (name == u"opaque"_s) {
        return QcRenderLayer::Opaque;
    }
    if (name == u"cutout"_s) {
        return QcRenderLayer::Cutout;
    }
    if (name == u"translucent"_s) {
        return QcRenderLayer::Translucent;
    }
    if (name == u"invisible"_s) {
        return QcRenderLayer::Invisible;
    }
    return std::nullopt;
}

QString localPart(const QString &qualifiedName)
{
    return qualifiedName.mid(qualifiedName.indexOf(u':') + 1);
}

// Recipe ingredients are item names or, with a leading '#', item tags.
QString qualifyIngredient(const QString &text, const QString &packNamespace)
{
    if (text.startsWith(u'#')) {
        const QString tag = QcContentNames::qualify(text.mid(1), packNamespace);
        return tag.isEmpty() ? QString() : u'#' + tag;
    }
    return QcContentNames::qualify(text, packNamespace);
}

QStringList parseTags(const QJsonValue &value, const QString &packNamespace, FieldReader &reader)
{
    QStringList tags;
    if (value.isUndefined()) {
        return tags;
    }
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > 16) {
        reader.fail(u"'tags' must be an array of at most 16 names"_s);
        return tags;
    }
    for (const auto &entry : list) {
        const QString tag = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (tag.isEmpty()) {
            reader.fail(u"invalid tag"_s);
            return {};
        }
        tags.append(tag);
    }
    return tags;
}

// Boxes are six numbers [x0, y0, z0, x1, y1, z1] in block-local metres.
QList<QcAabb> parseShapeBoxes(const QJsonValue &value, double maxY, const QString &key, FieldReader &reader)
{
    QList<QcAabb> boxes;
    if (value.isUndefined()) {
        return boxes;
    }
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > QcShapeDefinition::maxBoxesPerPart) {
        reader.fail(u"'%1' must be an array of at most %2 boxes"_s.arg(key).arg(QcShapeDefinition::maxBoxesPerPart));
        return boxes;
    }
    for (const auto &entry : list) {
        const QJsonArray numbers = entry.toArray();
        bool ok = entry.isArray() && numbers.size() == 6;
        std::array<double, 6> v{};
        for (int i = 0; ok && i < 6; ++i) {
            ok = numbers.at(i).isDouble();
            v[std::size_t(i)] = numbers.at(i).toDouble();
        }
        const QcAabb box{{v[0], v[1], v[2]}, {v[3], v[4], v[5]}};
        ok = ok && box.min.x >= 0.0 && box.max.x <= 1.0 && box.min.z >= 0.0 && box.max.z <= 1.0 && box.min.y >= 0.0
             && box.max.y <= maxY && box.min.x < box.max.x && box.min.y < box.max.y && box.min.z < box.max.z;
        if (!ok) {
            reader.fail(u"'%1' entries must be [x0, y0, z0, x1, y1, z1] with 0 <= from < to <= 1 (height up to %2)"_s.arg(key).arg(maxY));
            return {};
        }
        boxes.append(box);
    }
    return boxes;
}

// A state value from JSON: strings as they are, booleans as "true"/"false".
std::optional<QString> stateValue(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? u"true"_s : u"false"_s;
    }
    return std::nullopt;
}

QcShapePart parseShapePart(const QJsonObject &json, FieldReader &reader)
{
    QcShapePart part;
    FieldReader partReader{json, reader.context, {}};
    const QJsonValue when = json.value(u"when"_s);
    if (!when.isUndefined() && (!when.isObject() || when.toObject().size() > QcShapeDefinition::maxConditions)) {
        partReader.fail(u"'when' must be an object of at most %1 conditions"_s.arg(QcShapeDefinition::maxConditions));
    }
    const QJsonObject conditions = when.toObject();
    for (auto it = conditions.constBegin(); it != conditions.constEnd(); ++it) {
        QcShapeCondition condition;
        condition.property = it.key();
        const QJsonArray alternatives = it.value().isArray() ? it.value().toArray() : QJsonArray{it.value()};
        for (const auto &alternative : alternatives) {
            const std::optional<QString> value = stateValue(alternative);
            if (!value || condition.values.size() >= QcBlockProperty::maxValues) {
                partReader.fail(u"'when' values must be strings or booleans"_s);
                break;
            }
            condition.values.append(*value);
        }
        part.when.append(std::move(condition));
    }
    part.boxes = parseShapeBoxes(json.value(u"boxes"_s), 1.0, u"boxes"_s, partReader);
    part.crosses = parseShapeBoxes(json.value(u"crosses"_s), 1.0, u"crosses"_s, partReader);
    part.customCollision = json.contains(u"collision"_s);
    part.collision = parseShapeBoxes(json.value(u"collision"_s), QcShapeDefinition::maxCollisionHeight, u"collision"_s, partReader);
    part.texture = partReader.string(u"texture"_s);
    part.stretch = partReader.string(u"uv"_s, u"cell"_s) == u"box"_s;
    if (partReader.string(u"uv"_s, u"cell"_s) != u"cell"_s && !part.stretch) {
        partReader.fail(u"'uv' must be cell or box"_s);
    }
    if (part.texture.size() > 32) {
        partReader.fail(u"'texture' names a slot of at most 32 characters"_s);
    }
    if (!partReader.ok()) {
        reader.fail(partReader.error);
    }
    return part;
}

// Shape body shared by shape files and inline block shapes: "parts", or "boxes"/"crosses"/"collision"
// directly for a shape of one unconditional part.
void parseShapeBody(const QJsonObject &json, QcShapeDefinition &shape, FieldReader &reader)
{
    shape.rotate = reader.string(u"rotate"_s);
    shape.flip = reader.string(u"flip"_s);
    const QJsonValue parts = json.value(u"parts"_s);
    if (!parts.isUndefined()) {
        const QJsonArray list = parts.toArray();
        if (!parts.isArray() || list.isEmpty() || list.size() > QcShapeDefinition::maxParts) {
            reader.fail(u"'parts' must be an array of 1..%1 parts"_s.arg(QcShapeDefinition::maxParts));
            return;
        }
        for (const auto &entry : list) {
            if (!entry.isObject()) {
                reader.fail(u"'parts' entries must be objects"_s);
                return;
            }
            shape.parts.append(parseShapePart(entry.toObject(), reader));
        }
    } else {
        shape.parts.append(parseShapePart(json, reader));
    }
    const bool empty = std::ranges::all_of(shape.parts, [](const QcShapePart &part) {
        return part.boxes.isEmpty() && part.crosses.isEmpty();
    });
    if (reader.ok() && empty) {
        reader.fail(u"a shape needs at least one box or cross"_s);
    }
    if (reader.ok() && shape.boxCount() > QcShapeDefinition::maxBoxes) {
        reader.fail(u"a shape has at most %1 boxes, crosses and collision boxes in all parts together"_s.arg(QcShapeDefinition::maxBoxes));
    }
}

} // namespace

namespace QcContentParser {

QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"block"_s, {}};
    QcBlockDefinition block;
    block.name = reader.name(u"name"_s, packNamespace);
    if (block.name.isEmpty()) {
        return qcError(reader.ok() ? u"block: 'name' is required"_s : reader.error);
    }
    reader.context = u"block '%1'"_s.arg(block.name);
    const QString blockNamespace = block.name.left(block.name.indexOf(u':'));

    block.translationKey = reader.string(u"translation"_s,
                                         u"block.%1.%2"_s.arg(blockNamespace, localPart(block.name).replace(u'/', u'.')));
    block.descriptionKey = reader.string(u"description"_s, block.translationKey + u".description"_s);
    // "cube" and "cross" always name the base shapes; other names resolve in the pack's namespace.
    const QJsonValue shapeValue = json.value(u"shape"_s);
    if (shapeValue.isObject()) {
        const QJsonObject inlineShape = shapeValue.toObject();
        FieldReader shapeReader{inlineShape, reader.context + u" shape"_s, {}};
        parseShapeBody(inlineShape, block.shape, shapeReader);
        if (!shapeReader.ok()) {
            reader.fail(shapeReader.error);
        }
    } else if (shapeValue.isString()) {
        const QString shapeName = shapeValue.toString();
        if (shapeName == u"cross"_s || shapeName == u"cube"_s) {
            block.shapeName = u"qtcraft:"_s + shapeName;
        } else {
            block.shapeName = reader.name(u"shape"_s, packNamespace);
        }
        if (block.shapeName == u"qtcraft:cube"_s) {
            block.shapeName.clear();
        }
    } else if (!shapeValue.isUndefined()) {
        reader.fail(u"'shape' must be a shape name or an inline shape object"_s);
    }
    const bool cross = block.shapeName == u"qtcraft:cross"_s;
    const QString layerName = reader.string(u"render"_s, cross ? u"cutout"_s : u"opaque"_s);
    const std::optional<QcRenderLayer> layer = renderLayerFromName(layerName);
    if (!layer) {
        reader.fail(u"'render' must be opaque, cutout, translucent or invisible"_s);
    }
    block.renderLayer = layer.value_or(QcRenderLayer::Opaque);
    const bool fullCube = block.renderLayer == QcRenderLayer::Opaque && !cross;
    block.solid = reader.boolean(u"solid"_s, !cross);
    block.opaque = reader.boolean(u"opaque"_s, fullCube);
    if (cross && (block.opaque || block.renderLayer == QcRenderLayer::Opaque)) {
        reader.fail(u"cross-shaped blocks must use render cutout or translucent and cannot be opaque"_s);
    }
    block.liquid = reader.boolean(u"liquid"_s, false);
    block.replaceable = reader.boolean(u"replaceable"_s, false);
    const QJsonValue states = json.value(u"states"_s);
    if (!states.isUndefined() && (!states.isObject() || states.toObject().size() > QcBlockProperty::maxProperties)) {
        reader.fail(u"'states' must be an object of at most %1 properties"_s.arg(QcBlockProperty::maxProperties));
    }
    const QJsonObject stateJson = states.toObject();
    for (auto it = stateJson.constBegin(); it != stateJson.constEnd(); ++it) {
        QcBlockProperty property;
        property.name = it.key();
        const QJsonArray values = it.value().toArray();
        for (const auto &value : values) {
            const std::optional<QString> text = stateValue(value);
            if (!text) {
                reader.fail(u"state values must be strings or booleans"_s);
                break;
            }
            property.values.append(*text);
        }
        block.properties.append(std::move(property));
    }
    // The first listed value is the default; JSON keeps the order of array entries.
    if (const QString problem = QcBlockStateTable::problem(block); reader.ok() && !problem.isEmpty()) {
        reader.fail(problem);
    }
    block.lightLevel = reader.integer(u"light"_s, 0, 0, QcBlockDefinition::maxLightLevel);
    block.climbable = reader.boolean(u"climbable"_s, false);
    block.toggles = reader.boolean(u"toggles"_s, false);
    block.door = reader.boolean(u"door"_s, false);
    const QString facing = reader.string(u"facing"_s, u"away"_s);
    if (facing == u"toward"_s) {
        block.facingRule = QcFacingRule::Toward;
    } else if (facing != u"away"_s) {
        reader.fail(u"'facing' must be away or toward"_s);
    }
    const QString support = reader.string(u"support"_s, cross ? u"below"_s : u"none"_s);
    if (support == u"below"_s) {
        block.support = QcBlockSupport::Below;
    } else if (support == u"attached"_s) {
        block.support = QcBlockSupport::Attached;
    } else if (support != u"none"_s) {
        reader.fail(u"'support' must be none, below or attached"_s);
    }
    const QJsonValue connects = json.value(u"connects"_s);
    if (!connects.isUndefined() && !connects.isObject()) {
        reader.fail(u"'connects' must be an object"_s);
    } else if (connects.isObject()) {
        const QJsonObject connectJson = connects.toObject();
        FieldReader connectReader{connectJson, reader.context + u" connects"_s, {}};
        block.connectGroup = connectReader.string(u"group"_s);
        const QJsonArray targets = connectJson.value(u"to"_s).toArray();
        for (const auto &target : targets) {
            if (!target.isString() || target.toString().isEmpty() || target.toString().size() > 64 || block.connectsTo.size() >= 16) {
                connectReader.fail(u"'to' must list at most 16 group names"_s);
                break;
            }
            block.connectsTo.append(target.toString());
        }
        if (block.connectsTo.isEmpty() && !block.connectGroup.isEmpty()) {
            block.connectsTo.append(block.connectGroup);
        }
        if (block.connectGroup.size() > 64) {
            connectReader.fail(u"'group' is too long"_s);
        }
        if (!connectReader.ok()) {
            reader.fail(connectReader.error);
        }
    }
    const QJsonValue container = json.value(u"container"_s);
    if (!container.isUndefined() && !container.isObject()) {
        reader.fail(u"'container' must be an object"_s);
    } else if (container.isObject()) {
        const QJsonObject containerJson = container.toObject();
        FieldReader containerReader{containerJson, reader.context + u" container"_s, {}};
        block.containerSlots = containerReader.integer(u"slots"_s, 27, 1, QcBlockDefinition::maxContainerSlots);
        if (containerReader.ok() && (block.opensCrafting || json.contains(u"machine"_s))) {
            containerReader.fail(u"a block cannot store items and also be a crafting table or machine"_s);
        }
        if (!containerReader.ok()) {
            reader.fail(containerReader.error);
        }
    }
    block.lightOpacity = reader.integer(u"lightOpacity"_s, block.opaque ? 15 : (block.liquid ? 2 : 0), 0, 15);
    block.hardness = reader.number(u"hardness"_s, 1.0, -1.0, 1000.0);
    block.tool = reader.string(u"tool"_s);
    block.toolLevel = reader.integer(u"toolLevel"_s, 0, 0, 100);
    block.opensCrafting = reader.boolean(u"crafting"_s, false);
    block.fuelSeconds = reader.number(u"fuelSeconds"_s, 0.0, 0.0, maxFuelSeconds);
    const QJsonValue machine = json.value(u"machine"_s);
    if (!machine.isUndefined() && !machine.isObject()) {
        reader.fail(u"'machine' must be an object"_s);
    } else if (machine.isObject()) {
        const QJsonObject machineJson = machine.toObject();
        FieldReader machineReader{machineJson, reader.context + u" machine"_s, {}};
        block.machine.category = machineReader.string(u"category"_s, QString::fromLatin1(QcSmeltingRecipe::defaultCategory));
        block.machine.speed = machineReader.number(u"speed"_s, 1.0, minMachineSpeed, maxMachineSpeed);
        block.machine.sound = machineReader.name(u"sound"_s, packNamespace, QString::fromLatin1(QcSoundKeys::machineActive));
        if (machineReader.ok() && !isValidCategory(block.machine.category)) {
            machineReader.fail(invalidCategoryMessage());
        }
        if (machineReader.ok() && block.opensCrafting) {
            machineReader.fail(u"a block cannot be a crafting table and a machine"_s);
        }
        if (!machineReader.ok()) {
            reader.fail(machineReader.error);
        }
    }

    const QJsonValue drops = json.value(u"drops"_s);
    if (drops.isUndefined()) {
        block.drops = {{block.name, 1}};
    } else if (!drops.isArray() || drops.toArray().size() > 64) {
        reader.fail(u"'drops' must be an array of at most 64 entries"_s);
    } else {
        const QJsonArray dropList = drops.toArray();
        for (const auto &entry : dropList) {
            if (!entry.isObject()) {
                reader.fail(u"'drops' entries must be objects"_s);
                break;
            }
            const QJsonObject dropJson = entry.toObject();
            FieldReader dropReader{dropJson, reader.context, {}};
            const QcItemDrop drop{dropReader.name(u"item"_s, packNamespace), dropReader.integer(u"count"_s, 1, 1, 64),
                                  dropReader.number(u"chance"_s, 1.0, 0.0, 1.0)};
            if (!dropReader.ok() || drop.item.isEmpty()) {
                reader.fail(dropReader.ok() ? u"drop needs an 'item'"_s : dropReader.error);
                break;
            }
            block.drops.append(drop);
        }
    }

    const QJsonValue sapling = json.value(u"sapling"_s);
    if (!sapling.isUndefined() && !sapling.isObject()) {
        reader.fail(u"'sapling' must be an object"_s);
    } else if (sapling.isObject()) {
        const QJsonObject saplingJson = sapling.toObject();
        FieldReader saplingReader{saplingJson, reader.context + u" sapling"_s, {}};
        QcSaplingDefinition &definition = block.sapling;
        definition.feature = saplingReader.name(u"feature"_s, packNamespace);
        definition.minSeconds = saplingReader.number(u"minSeconds"_s, 120.0, 1.0, 86400.0);
        definition.maxSeconds = saplingReader.number(u"maxSeconds"_s, std::max(300.0, definition.minSeconds),
                                                     definition.minSeconds, 86400.0);
        definition.plantChance = saplingReader.number(u"plantChance"_s, 0.0, 0.0, 1.0);
        const QJsonValue plantOn = saplingJson.value(u"plantOn"_s);
        if (!plantOn.isUndefined() && (!plantOn.isArray() || plantOn.toArray().size() > 64)) {
            saplingReader.fail(u"'plantOn' must be an array of at most 64 block names"_s);
        }
        const QJsonArray plantOnList = plantOn.toArray();
        for (const auto &entry : plantOnList) {
            const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
            if (name.isEmpty()) {
                saplingReader.fail(u"invalid 'plantOn' entry"_s);
                break;
            }
            definition.plantOn.append(name);
        }
        if (saplingReader.ok() && definition.feature.isEmpty()) {
            saplingReader.fail(u"'feature' is required"_s);
        }
        if (saplingReader.ok() && block.solid) {
            saplingReader.fail(u"saplings must not be solid"_s);
        }
        if (!saplingReader.ok()) {
            reader.fail(saplingReader.error);
        }
    }

    const QJsonValue flow = json.value(u"flow"_s);
    if (!flow.isUndefined() && (!flow.isObject() || !block.liquid)) {
        reader.fail(u"'flow' must be an object and needs 'liquid': true"_s);
    } else if (flow.isObject()) {
        const QJsonObject flowJson = flow.toObject();
        FieldReader flowReader{flowJson, reader.context + u" flow"_s, {}};
        QcLiquidDefinition &liquid = block.flow;
        liquid.distance = flowReader.integer(u"distance"_s, 7, 1, QcLiquidDefinition::maxDistance);
        liquid.ticks = flowReader.integer(u"ticks"_s, 5, 1, QcLiquidDefinition::maxTicks);
        liquid.infinite = flowReader.boolean(u"infinite"_s, false);
        liquid.movementFactor = flowReader.number(u"movement"_s, 1.0, 0.05, 2.0);
        liquid.bucket = flowReader.name(u"bucket"_s, packNamespace);
        liquid.sound = flowReader.name(u"sound"_s, packNamespace);
        const QJsonValue reactions = flowJson.value(u"reactions"_s);
        if (!reactions.isUndefined() && (!reactions.isArray() || reactions.toArray().size() > 16)) {
            flowReader.fail(u"'reactions' must be an array of at most 16 entries"_s);
        }
        const QJsonArray reactionList = reactions.toArray();
        for (const auto &entry : reactionList) {
            const QJsonObject reactionJson = entry.toObject();
            FieldReader reactionReader{reactionJson, flowReader.context, {}};
            const QcLiquidReaction reaction{reactionReader.name(u"with"_s, packNamespace), reactionReader.name(u"source"_s, packNamespace),
                                            reactionReader.name(u"flowing"_s, packNamespace)};
            if (!entry.isObject() || !reactionReader.ok() || reaction.with.isEmpty() || reaction.source.isEmpty()
                || reaction.flowing.isEmpty()) {
                flowReader.fail(reactionReader.ok() ? u"reactions need 'with', 'source' and 'flowing'"_s : reactionReader.error);
                break;
            }
            liquid.reactions.append(reaction);
        }
        // The flow keeps its state in `level` ("0" source, then one value per block of distance) and
        // optionally `falling`.
        const QcBlockProperty *level = block.property(u"level"_s);
        bool levelsOk = level && level->values.size() >= liquid.distance + 1;
        for (int i = 0; levelsOk && i < level->values.size(); ++i) {
            levelsOk = level->values[i] == QString::number(i);
        }
        if (flowReader.ok() && !levelsOk) {
            flowReader.fail(u"a flowing liquid needs the state 'level' with the values \"0\" to \"%1\" in order"_s.arg(liquid.distance));
        }
        const QcBlockProperty *falling = block.property(u"falling"_s);
        if (flowReader.ok() && falling && falling->values != QStringList{u"false"_s, u"true"_s}) {
            flowReader.fail(u"'falling' must have the values false and true"_s);
        }
        if (!flowReader.ok()) {
            reader.fail(flowReader.error);
        }
    }
    block.contactDamage = reader.number(u"contactDamage"_s, 0.0, 0.0, 100.0);
    block.washable = reader.boolean(u"washable"_s, cross);
    block.falls = reader.boolean(u"falls"_s, false);
    if (block.falls && (block.liquid || !block.solid)) {
        reader.fail(u"only solid blocks can fall"_s);
    }
    const QJsonValue ground = json.value(u"ground"_s);
    if (!ground.isUndefined() && (!ground.isArray() || ground.toArray().size() > 64)) {
        reader.fail(u"'ground' must be an array of at most 64 block names"_s);
    }
    const QJsonArray groundList = ground.toArray();
    for (const auto &entry : groundList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty()) {
            reader.fail(u"invalid 'ground' entry"_s);
            break;
        }
        block.groundBlocks.append(name);
    }
    block.groundLiquid = reader.name(u"groundLiquid"_s, packNamespace);
    if ((!block.groundBlocks.isEmpty() || !block.groundLiquid.isEmpty()) && block.support != QcBlockSupport::Below) {
        reader.fail(u"'ground' and 'groundLiquid' need 'support': \"below\""_s);
    }

    block.soundMaterial = reader.name(u"sound"_s, packNamespace, QcSoundKeys::defaultMaterialFor(block));
    const QJsonValue sounds = json.value(u"sounds"_s);
    if (!sounds.isUndefined() && !sounds.isObject()) {
        reader.fail(u"'sounds' must be an object"_s);
    } else if (sounds.isObject()) {
        const QJsonObject soundJson = sounds.toObject();
        FieldReader soundReader{soundJson, reader.context + u" sounds"_s, {}};
        for (int action = 0; action < qcBlockSoundActionCount; ++action) {
            block.soundKeys[std::size_t(action)] =
                soundReader.name(QcSoundKeys::actionName(QcBlockSoundAction(action)), packNamespace);
        }
        if (!soundReader.ok()) {
            reader.fail(soundReader.error);
        }
    }

    const QJsonValue textures = json.value(u"textures"_s);
    if (block.renderLayer != QcRenderLayer::Invisible) {
        if (!textures.isObject()) {
            reader.fail(u"'textures' object is required for visible blocks"_s);
        } else {
            const QJsonObject faces = textures.toObject();
            FieldReader textureReader{faces, reader.context + u" textures"_s, {}};
            const QString all = textureReader.name(u"all"_s, packNamespace);
            const QString side = textureReader.name(u"side"_s, packNamespace, all);
            const QString top = textureReader.name(u"top"_s, packNamespace, all);
            const QString bottom = textureReader.name(u"bottom"_s, packNamespace, all);
            const auto face = [&](const QString &key, const QString &fallback) {
                return textureReader.name(key, packNamespace, fallback);
            };
            block.faceTextures = {face(u"east"_s, side), face(u"west"_s, side), top,
                                  bottom, face(u"south"_s, side), face(u"north"_s, side)};
            const QJsonObject slotJson = faces.value(u"slots"_s).toObject();
            if (slotJson.size() > 16 || (faces.contains(u"slots"_s) && !faces.value(u"slots"_s).isObject())) {
                textureReader.fail(u"'slots' must be an object of at most 16 named textures"_s);
            }
            for (auto it = slotJson.constBegin(); it != slotJson.constEnd(); ++it) {
                const QString texture = it.value().isString() ? QcContentNames::qualify(it.value().toString(), packNamespace) : QString();
                if (texture.isEmpty() || it.key().isEmpty() || it.key().size() > 32) {
                    textureReader.fail(u"invalid texture slot '%1'"_s.arg(it.key()));
                    break;
                }
                block.textureSlots.append({it.key(), texture});
            }
            if (!textureReader.ok()) {
                reader.fail(textureReader.error);
            }
            for (const QString &texture : block.faceTextures) {
                if (texture.isEmpty()) {
                    reader.fail(u"every face needs a texture (use 'all', 'side', 'top', 'bottom' or a face name)"_s);
                    break;
                }
            }
        }
    }

    block.itemTexture = reader.name(u"itemTexture"_s, packNamespace);
    block.itemTags = parseTags(json.value(u"tags"_s), packNamespace, reader);
    for (const QcShapePart &part : std::as_const(block.shape.parts)) {
        if (!part.texture.isEmpty() && block.slotTexture(part.texture).isEmpty() && block.renderLayer != QcRenderLayer::Invisible) {
            reader.fail(u"shape part uses the unknown texture slot '%1'"_s.arg(part.texture));
            break;
        }
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return block;
}

QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"shape"_s, {}};
    QcShapeDefinition shape;
    shape.name = reader.name(u"name"_s, packNamespace);
    if (shape.name.isEmpty()) {
        return qcError(reader.ok() ? u"shape: 'name' is required"_s : reader.error);
    }
    reader.context = u"shape '%1'"_s.arg(shape.name);
    parseShapeBody(json, shape, reader);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return shape;
}

QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"part family"_s, {}};
    QcPartFamily family;
    family.name = reader.name(u"name"_s, packNamespace);
    if (family.name.isEmpty()) {
        return qcError(reader.ok() ? u"part family: 'name' is required"_s : reader.error);
    }
    reader.context = u"part family '%1'"_s.arg(family.name);
    family.suffix = reader.string(u"suffix"_s, localPart(family.name));
    if (!QcContentNames::isValidPart(family.suffix) || family.suffix.contains(u'/') || family.suffix.size() > 32) {
        reader.fail(u"'suffix' must be a plain name of at most 32 characters"_s);
    }
    const QJsonArray kinds = json.value(u"kinds"_s).toArray();
    for (const auto &kind : kinds) {
        if (!kind.isString() || kind.toString().isEmpty() || kind.toString().size() > 32 || family.kinds.size() >= 16) {
            reader.fail(u"'kinds' must list at most 16 material kinds"_s);
            break;
        }
        family.kinds.append(kind.toString());
    }
    if (family.kinds.isEmpty()) {
        reader.fail(u"'kinds' needs at least one material kind"_s);
    }
    family.hardnessFactor = reader.number(u"hardness"_s, 1.0, 0.0, 100.0);
    family.fuelFactor = reader.number(u"fuel"_s, 1.0, 0.0, 100.0);
    family.block = json.value(u"block"_s).toObject();
    family.recipe = json.value(u"recipe"_s).toObject();
    if (!json.value(u"block"_s).isObject() || !json.value(u"recipe"_s).isObject()) {
        reader.fail(u"'block' and 'recipe' objects are required"_s);
    }
    const QJsonObject translations = json.value(u"translations"_s).toObject();
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        if (!it.value().isString() || !it.value().toString().contains(u"%1"_s) || it.value().toString().size() > 128
            || it.key().size() > 16 || family.translations.size() >= 64) {
            reader.fail(u"'translations' maps languages to texts with %1 for the material"_s);
            break;
        }
        family.translations.append({it.key(), it.value().toString()});
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    // Check the templates once with a stand-in material, so broken families fail at loading.
    QJsonObject sample = family.block;
    sample.insert(u"name"_s, u"qtcraft:sample_part"_s);
    sample.insert(u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:sample"_s}});
    if (const auto block = parseBlock(sample, packNamespace); !block) {
        return qcError(reader.context + u" block: "_s + block.error());
    }
    if (const auto recipe = parseRecipe(partRecipeJson(family, u"qtcraft:sample"_s, u"qtcraft:sample_part"_s), packNamespace); !recipe) {
        return qcError(reader.context + u" recipe: "_s + recipe.error());
    }
    return family;
}

QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"material"_s, {}};
    QcMaterialDefinition material;
    material.name = reader.name(u"name"_s, packNamespace);
    if (material.name.isEmpty()) {
        return qcError(reader.ok() ? u"material: 'name' is required"_s : reader.error);
    }
    reader.context = u"material '%1'"_s.arg(material.name);
    material.excluded = reader.boolean(u"excluded"_s, false);
    if (material.excluded) {
        const QJsonArray blocks = json.value(u"blocks"_s).toArray();
        for (const auto &entry : blocks) {
            const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
            if (name.isEmpty() || material.excludedBlocks.size() >= 1024) {
                reader.fail(u"'blocks' must list at most 1024 block names"_s);
                break;
            }
            material.excludedBlocks.append(name);
        }
        if (reader.ok() && material.excludedBlocks.isEmpty()) {
            reader.fail(u"an exclusion needs 'blocks'"_s);
        }
    } else {
        material.block = reader.name(u"block"_s, packNamespace);
        material.kind = reader.string(u"kind"_s);
        if (reader.ok() && (material.block.isEmpty() || material.kind.isEmpty() || material.kind.size() > 32)) {
            reader.fail(u"'block' and 'kind' are required"_s);
        }
        const QJsonArray exclude = json.value(u"exclude"_s).toArray();
        for (const auto &entry : exclude) {
            const QString family = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
            if (family.isEmpty() || material.exclude.size() >= 64) {
                reader.fail(u"'exclude' must list at most 64 part families"_s);
                break;
            }
            material.exclude.append(family);
        }
    }
    material.translationKey = reader.string(
        u"translation"_s,
        u"material.%1.%2"_s.arg(material.name.left(material.name.indexOf(u':')), localPart(material.name).replace(u'/', u'.')));
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return material;
}

QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName)
{
    QJsonObject keys;
    const QJsonObject familyKeys = family.recipe.value(u"key"_s).toObject();
    for (auto it = familyKeys.constBegin(); it != familyKeys.constEnd(); ++it) {
        keys.insert(it.key(), it.value().toString() == u"@"_s ? QJsonValue(baseBlock) : it.value());
    }
    return QJsonObject{{u"name"_s, partName},
                       {u"type"_s, u"shaped"_s},
                       {u"pattern"_s, family.recipe.value(u"pattern"_s)},
                       {u"key"_s, keys},
                       {u"result"_s, QJsonObject{{u"item"_s, partName}, {u"count"_s, family.recipe.value(u"count"_s).toInt(1)}}}};
}

QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"texture"_s, {}};
    QcTextureDefinition texture;
    texture.name = reader.name(u"name"_s, packNamespace);
    if (texture.name.isEmpty()) {
        return qcError(reader.ok() ? u"texture: 'name' is required"_s : reader.error);
    }
    if (!json.value(u"pattern"_s).isString()) {
        return qcError(u"texture '%1': 'pattern' string is required"_s.arg(texture.name));
    }
    // Only the fields the synthesizer reads are kept (short strings and numbers), so a texture
    // definition stays small whatever else the file contains.
    for (const QString &key : {u"pattern"_s, u"primary"_s, u"secondary"_s}) {
        const QJsonValue value = json.value(key);
        if (value.isString() && value.toString().size() <= 32) {
            texture.procedural.insert(key, value);
        } else if (!value.isUndefined()) {
            return qcError(u"texture '%1': '%2' must be a short string"_s.arg(texture.name, key));
        }
    }
    for (const QString &key : {u"variation"_s, u"seed"_s}) {
        const QJsonValue value = json.value(key);
        if (value.isDouble()) {
            texture.procedural.insert(key, value);
        } else if (!value.isUndefined()) {
            return qcError(u"texture '%1': '%2' must be a number"_s.arg(texture.name, key));
        }
    }
    return texture;
}

QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json)
{
    QHash<QString, QString> entries;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!it.value().isString()) {
            return qcError(u"translation '%1' must be a string"_s.arg(it.key()));
        }
        entries.insert(it.key(), it.value().toString());
    }
    return entries;
}

namespace {

std::optional<QcVec3> vectorField(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 3 || !array.at(0).isDouble() || !array.at(1).isDouble() || !array.at(2).isDouble()) {
        return std::nullopt;
    }
    const QcVec3 vector{array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble()};
    constexpr double limit = 8.0;
    if (std::abs(vector.x) > limit || std::abs(vector.y) > limit || std::abs(vector.z) > limit) {
        return std::nullopt;
    }
    return vector;
}

QList<QcModelBox> parseBoxes(const QJsonValue &value, int limit, FieldReader &reader)
{
    QList<QcModelBox> boxes;
    if (!value.isArray() || value.toArray().size() > limit) {
        reader.fail(u"'boxes' must be an array of at most %1 boxes"_s.arg(limit));
        return boxes;
    }
    const QJsonArray list = value.toArray();
    for (const auto &entry : list) {
        const QJsonObject boxJson = entry.toObject();
        const std::optional<QcVec3> from = vectorField(boxJson.value(u"from"_s));
        const std::optional<QcVec3> to = vectorField(boxJson.value(u"to"_s));
        const QString slot = boxJson.value(u"slot"_s).toString();
        if (!from || !to || slot.isEmpty() || slot.size() > 64 || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"boxes need 'from' < 'to' (three numbers within 8 m) and a 'slot'"_s);
            return {};
        }
        boxes.append({*from, *to, slot});
    }
    return boxes;
}

std::optional<QcBoneRole> boneRoleFromName(const QString &name)
{
    static const QHash<QString, QcBoneRole> roles = {
        {u"none"_s, QcBoneRole::None},          {u"head"_s, QcBoneRole::Head},
        {u"body"_s, QcBoneRole::Body},          {u"right_arm"_s, QcBoneRole::RightArm},
        {u"left_arm"_s, QcBoneRole::LeftArm},   {u"right_leg"_s, QcBoneRole::RightLeg},
        {u"left_leg"_s, QcBoneRole::LeftLeg},
    };
    const auto it = roles.constFind(name);
    return it == roles.constEnd() ? std::nullopt : std::optional<QcBoneRole>(*it);
}

std::optional<QcTint> tintFromName(const QString &name)
{
    static const QHash<QString, QcTint> tints = {
        {u"none"_s, QcTint::None}, {u"skin"_s, QcTint::Skin}, {u"hair"_s, QcTint::Hair},
        {u"shirt"_s, QcTint::Shirt}, {u"pants"_s, QcTint::Pants},
    };
    const auto it = tints.constFind(name);
    return it == tints.constEnd() ? std::nullopt : std::optional<QcTint>(*it);
}

QList<QcItemDrop> parseDrops(const QJsonValue &drops, const QString &packNamespace, FieldReader &reader)
{
    QList<QcItemDrop> result;
    if (drops.isUndefined()) {
        return result;
    }
    if (!drops.isArray()) {
        reader.fail(u"'drops' must be an array"_s);
        return result;
    }
    const QJsonArray list = drops.toArray();
    for (const auto &entry : list) {
        const QJsonObject dropJson = entry.toObject();
        FieldReader dropReader{dropJson, reader.context, {}};
        const QcItemDrop drop{dropReader.name(u"item"_s, packNamespace), dropReader.integer(u"count"_s, 1, 1, 64)};
        if (!entry.isObject() || !dropReader.ok() || drop.item.isEmpty()) {
            reader.fail(dropReader.ok() ? u"drop needs an 'item'"_s : dropReader.error);
            break;
        }
        result.append(drop);
    }
    return result;
}

// One spawn rule of a creature: the `spawn` object itself or an entry of its `also` list.
QcMobDefinition::SpawnRule parseSpawnRule(const QJsonObject &spawn, FieldReader &reader, bool hostile, const QString &packNamespace)
{
    QcMobDefinition::SpawnRule rule;
    const QString time = reader.string(u"time"_s, u"any"_s);
    if (time == u"day"_s) {
        rule.time = QcMobDefinition::SpawnTime::Day;
    } else if (time == u"night"_s) {
        rule.time = QcMobDefinition::SpawnTime::Night;
    } else if (time != u"any"_s) {
        reader.fail(u"'time' must be day, night or any"_s);
    }
    rule.weight = reader.number(u"weight"_s, 1.0, 0.0, 100.0);
    rule.caves = reader.boolean(u"caves"_s, false);
    // Hostile creatures keep away from lit places unless their data says otherwise.
    rule.maxLight = reader.integer(u"maxLight"_s, hostile ? 7 : 15, 0, 15);
    rule.chunkChance = reader.number(u"chunkChance"_s, 1.0, 0.0, 1.0);
    rule.maxY = reader.integer(u"maxY"_s, QcWorld::worldHeight, 1, QcWorld::worldHeight);
    const QJsonArray blocks = spawn.value(u"blocks"_s).toArray();
    for (const auto &entry : blocks) {
        const QString name = QcContentNames::qualify(entry.toString(), packNamespace);
        if (name.isEmpty()) {
            reader.fail(u"invalid 'blocks' entry"_s);
            break;
        }
        rule.blocks.append(name);
    }
    const QJsonValue biomes = spawn.value(u"biomes"_s);
    if (!biomes.isUndefined() && (!biomes.isArray() || biomes.toArray().size() > 64)) {
        reader.fail(u"'biomes' must be an array of at most 64 biome names"_s);
    }
    const QJsonArray biomeList = biomes.toArray();
    for (const auto &entry : biomeList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty()) {
            reader.fail(u"invalid 'biomes' entry"_s);
            break;
        }
        rule.biomes.append(name);
    }
    return rule;
}

} // namespace

QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"mob"_s, {}};
    QcMobDefinition mob;
    mob.name = reader.name(u"name"_s, packNamespace);
    if (mob.name.isEmpty()) {
        return qcError(reader.ok() ? u"mob: 'name' is required"_s : reader.error);
    }
    reader.context = u"mob '%1'"_s.arg(mob.name);
    mob.translationKey = reader.string(
        u"translation"_s, u"mob.%1.%2"_s.arg(mob.name.left(mob.name.indexOf(u':')), localPart(mob.name).replace(u'/', u'.')));
    mob.descriptionKey = reader.string(u"description"_s, mob.translationKey + u".description"_s);
    mob.maxHealth = reader.number(u"health"_s, 10.0, 1.0, 1000.0);
    mob.width = reader.number(u"width"_s, 0.6, 0.2, 4.0);
    mob.height = reader.number(u"height"_s, 1.8, 0.2, 4.0);
    mob.walkSpeed = reader.number(u"walkSpeed"_s, 1.5, 0.0, 20.0);
    mob.chaseSpeed = reader.number(u"chaseSpeed"_s, mob.walkSpeed * 2.0, 0.0, 20.0);
    mob.hostile = reader.boolean(u"hostile"_s, false);
    mob.attackDamage = reader.number(u"attackDamage"_s, 2.0, 0.0, 100.0);
    mob.attackRange = reader.number(u"attackRange"_s, 1.6, 0.5, 8.0);
    mob.attackCooldown = reader.number(u"attackCooldown"_s, 1.0, 0.1, 60.0);
    mob.sightRange = reader.number(u"sightRange"_s, 16.0, 1.0, 64.0);

    const QJsonObject spawn = json.value(u"spawn"_s).toObject();
    FieldReader spawnReader{spawn, reader.context + u" spawn"_s, {}};
    const QcMobDefinition::SpawnRule main = parseSpawnRule(spawn, spawnReader, mob.hostile, packNamespace);
    mob.spawnTime = main.time;
    mob.spawnBlocks = main.blocks;
    mob.spawnWeight = main.weight;
    mob.spawnsInCaves = main.caves;
    mob.maxSpawnLight = main.maxLight;
    mob.spawnChunkChance = main.chunkChance;
    mob.spawnMaxY = main.maxY;
    mob.spawnBiomes = main.biomes;
    mob.maxGroup = spawnReader.integer(u"maxGroup"_s, 2, 1, 4);
    mob.ambient = spawnReader.boolean(u"ambient"_s, false);
    mob.maxCount = spawnReader.integer(u"maxCount"_s, 0, 0, 64);
    // Further places and times with their own rules; group size and caps stay shared.
    const QJsonValue also = spawn.value(u"also"_s);
    if (!also.isUndefined() && (!also.isArray() || also.toArray().size() > QcMobDefinition::maxExtraSpawns)) {
        spawnReader.fail(u"'also' must be an array of at most %1 spawn rules"_s.arg(QcMobDefinition::maxExtraSpawns));
    }
    const QJsonArray alsoList = also.toArray();
    for (const auto &entry : alsoList) {
        if (!entry.isObject()) {
            spawnReader.fail(u"'also' entries must be objects"_s);
            break;
        }
        FieldReader ruleReader{entry.toObject(), spawnReader.context + u" also"_s, {}};
        // Group size and caps belong to the creature, not to one rule.
        for (const QString &shared : {u"maxGroup"_s, u"ambient"_s, u"maxCount"_s}) {
            if (entry.toObject().contains(shared)) {
                ruleReader.fail(u"'%1' belongs to the main spawn rule"_s.arg(shared));
            }
        }
        mob.extraSpawns.append(parseSpawnRule(entry.toObject(), ruleReader, mob.hostile, packNamespace));
        if (!ruleReader.ok()) {
            spawnReader.fail(ruleReader.error);
            break;
        }
    }
    if (!spawnReader.ok()) {
        reader.fail(spawnReader.error);
    }
    if (mob.ambient && mob.hostile) {
        reader.fail(u"ambient creatures cannot be hostile"_s);
    }
    mob.flying = reader.boolean(u"flying"_s, false);
    mob.glow = reader.integer(u"glow"_s, 0, 0, QcMobDefinition::maxGlow);
    const QJsonValue catchValue = json.value(u"catch"_s);
    if (!catchValue.isUndefined() && !catchValue.isObject()) {
        reader.fail(u"'catch' must be an object"_s);
    } else if (catchValue.isObject()) {
        const QJsonObject catchJson = catchValue.toObject();
        FieldReader catchReader{catchJson, reader.context + u" catch"_s, {}};
        mob.catchWith = catchReader.name(u"with"_s, packNamespace);
        mob.catchItem = catchReader.name(u"item"_s, packNamespace);
        if (catchReader.ok() && (mob.catchWith.isEmpty() || mob.catchItem.isEmpty())) {
            catchReader.fail(u"needs 'with' and 'item'"_s);
        }
        if (!catchReader.ok()) {
            reader.fail(catchReader.error);
        }
    }

    mob.skeleton = reader.name(u"skeleton"_s, packNamespace);
    mob.outfit = reader.name(u"outfit"_s, packNamespace);
    mob.armsForward = reader.boolean(u"armsForward"_s, false);
    if (mob.skeleton.isEmpty() != mob.outfit.isEmpty()) {
        reader.fail(u"'skeleton' and 'outfit' go together"_s);
    }
    const QJsonArray model = json.value(u"model"_s).toArray();
    if (model.size() > 256) {
        reader.fail(u"'model' has more than 256 parts"_s);
    }
    for (const auto &entry : model) {
        const QJsonObject partJson = entry.toObject();
        const std::optional<QcVec3> from = vectorField(partJson.value(u"from"_s));
        const std::optional<QcVec3> to = vectorField(partJson.value(u"to"_s));
        const QString texture = QcContentNames::qualify(partJson.value(u"texture"_s).toString(), packNamespace);
        if (!from || !to || texture.isEmpty() || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"model parts need 'from' < 'to' (three numbers within 8 m) and a 'texture'"_s);
            break;
        }
        mob.parts.append({*from, *to, texture});
    }
    if (mob.parts.isEmpty() && mob.skeleton.isEmpty()) {
        reader.fail(u"'model' needs at least one part (or use 'skeleton' and 'outfit')"_s);
    }
    mob.drops = parseDrops(json.value(u"drops"_s), packNamespace, reader);
    const QJsonValue sounds = json.value(u"sounds"_s);
    if (!sounds.isUndefined() && !sounds.isObject()) {
        reader.fail(u"'sounds' must be an object"_s);
    }
    const QJsonObject soundJson = sounds.toObject();
    FieldReader soundReader{soundJson, reader.context + u" sounds"_s, {}};
    mob.ambientSound = soundReader.name(u"ambient"_s, packNamespace);
    mob.hurtSound = soundReader.name(u"hurt"_s, packNamespace, QString::fromLatin1(QcSoundKeys::mobHurt));
    mob.deathSound = soundReader.name(u"death"_s, packNamespace, QString::fromLatin1(QcSoundKeys::mobDeath));
    if (!soundReader.ok()) {
        reader.fail(soundReader.error);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return mob;
}

QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"model"_s, {}};
    QcModelDefinition model;
    model.name = reader.name(u"name"_s, packNamespace);
    if (model.name.isEmpty()) {
        return qcError(reader.ok() ? u"model: 'name' is required"_s : reader.error);
    }
    reader.context = u"model '%1'"_s.arg(model.name);
    const QJsonValue bones = json.value(u"bones"_s);
    if (!bones.isArray() || bones.toArray().isEmpty() || bones.toArray().size() > QcModelDefinition::maxBones) {
        return qcError(u"%1: 'bones' must be an array of 1 to %2 bones"_s.arg(reader.context).arg(QcModelDefinition::maxBones));
    }
    const QJsonArray boneList = bones.toArray();
    for (const auto &entry : boneList) {
        const QJsonObject boneJson = entry.toObject();
        FieldReader boneReader{boneJson, reader.context + u" bone"_s, {}};
        QcModelBone bone;
        bone.name = boneReader.string(u"name"_s);
        const QString role = boneReader.string(u"role"_s, u"none"_s);
        const std::optional<QcBoneRole> parsedRole = boneRoleFromName(role);
        if (!parsedRole) {
            boneReader.fail(u"'role' must be none, head, body, right_arm, left_arm, right_leg or left_leg"_s);
        }
        bone.role = parsedRole.value_or(QcBoneRole::None);
        const std::optional<QcVec3> pivot = vectorField(boneJson.value(u"pivot"_s));
        if (!pivot) {
            boneReader.fail(u"'pivot' must be three numbers within 8 m"_s);
        }
        bone.pivot = pivot.value_or(QcVec3{});
        bone.boxes = parseBoxes(boneJson.value(u"boxes"_s), QcModelDefinition::maxBoxesPerBone, boneReader);
        if (bone.name.isEmpty() || bone.name.size() > 64 || bone.boxes.isEmpty()) {
            boneReader.fail(u"bones need a 'name' (at most 64 characters) and at least one box"_s);
        }
        if (!boneReader.ok()) {
            return qcError(boneReader.error);
        }
        model.bones.append(std::move(bone));
    }
    return model;
}

QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"appearance"_s, {}};
    QcAppearanceDefinition appearance;
    appearance.name = reader.name(u"name"_s, packNamespace);
    if (appearance.name.isEmpty()) {
        return qcError(reader.ok() ? u"appearance: 'name' is required"_s : reader.error);
    }
    reader.context = u"appearance '%1'"_s.arg(appearance.name);
    appearance.translationKey = reader.string(u"translation"_s, u"appearance.%1.%2"_s.arg(
        appearance.name.left(appearance.name.indexOf(u':')), localPart(appearance.name).replace(u'/', u'.')));
    const QString kind = reader.string(u"kind"_s, u"outfit"_s);
    if (kind == u"hair"_s) {
        appearance.kind = QcAppearanceDefinition::Kind::Hair;
    } else if (kind != u"outfit"_s) {
        reader.fail(u"'kind' must be outfit or hair"_s);
    }
    appearance.playable = reader.boolean(u"playable"_s, true);
    const QJsonValue layers = json.value(u"layers"_s);
    if (!layers.isObject() || layers.toObject().isEmpty() || layers.toObject().size() > QcAppearanceDefinition::maxLayers) {
        reader.fail(u"'layers' must map 1 to %1 slots to textures"_s.arg(QcAppearanceDefinition::maxLayers));
    }
    const QJsonObject layerMap = layers.toObject();
    for (auto it = layerMap.constBegin(); it != layerMap.constEnd() && reader.ok(); ++it) {
        const QJsonObject layerJson = it.value().toObject();
        FieldReader layerReader{layerJson, u"%1 layer '%2'"_s.arg(reader.context, it.key()), {}};
        QcAppearanceLayer layer;
        layer.slot = it.key();
        layer.texture = layerReader.name(u"texture"_s, packNamespace);
        const std::optional<QcTint> tint = tintFromName(layerReader.string(u"tint"_s, u"none"_s));
        if (!tint) {
            layerReader.fail(u"'tint' must be none, skin, hair, shirt or pants"_s);
        }
        layer.tint = tint.value_or(QcTint::None);
        if (!it.value().isObject() || layer.texture.isEmpty() || layer.slot.size() > 64) {
            layerReader.fail(u"needs a 'texture'"_s);
        }
        if (!layerReader.ok()) {
            reader.fail(layerReader.error);
        }
        appearance.layers.append(std::move(layer));
    }
    if (appearance.kind == QcAppearanceDefinition::Kind::Hair) {
        appearance.boxes = parseBoxes(json.value(u"boxes"_s), QcAppearanceDefinition::maxBoxes, reader);
        if (reader.ok() && appearance.boxes.isEmpty()) {
            reader.fail(u"hair styles need 'boxes'"_s);
        }
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return appearance;
}

QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"recipe"_s, {}};
    QcRecipeDefinition recipe;
    recipe.name = reader.name(u"name"_s, packNamespace);
    if (recipe.name.isEmpty()) {
        return qcError(reader.ok() ? u"recipe: 'name' is required"_s : reader.error);
    }
    reader.context = u"recipe '%1'"_s.arg(recipe.name);
    const QString type = reader.string(u"type"_s, u"shaped"_s);
    recipe.shaped = type == u"shaped"_s;
    if (!recipe.shaped && type != u"shapeless"_s) {
        reader.fail(u"'type' must be shaped or shapeless"_s);
    }
    const QJsonObject result = json.value(u"result"_s).toObject();
    FieldReader resultReader{result, reader.context + u" result"_s, {}};
    recipe.resultItem = resultReader.name(u"item"_s, packNamespace);
    recipe.resultCount = resultReader.integer(u"count"_s, 1, 1, 64);
    if (!resultReader.ok() || recipe.resultItem.isEmpty()) {
        reader.fail(resultReader.ok() ? u"'result' needs an 'item'"_s : resultReader.error);
    }

    if (recipe.shaped) {
        QList<QString> rows;
        const QJsonArray pattern = json.value(u"pattern"_s).toArray();
        for (const auto &row : pattern) {
            rows.append(row.toString());
        }
        // Trim blank rows and columns so the shape can sit anywhere in the grid.
        while (!rows.isEmpty() && rows.first().trimmed().isEmpty()) {
            rows.removeFirst();
        }
        while (!rows.isEmpty() && rows.last().trimmed().isEmpty()) {
            rows.removeLast();
        }
        qsizetype width = 0;
        for (const QString &row : std::as_const(rows)) {
            width = std::max(width, row.size());
        }
        for (QString &row : rows) {
            row = row.leftJustified(width, u' ');
        }
        const auto columnBlank = [&rows](qsizetype column) {
            return std::ranges::all_of(rows, [column](const QString &row) { return row[column] == u' '; });
        };
        while (width > 0 && columnBlank(0)) {
            for (QString &row : rows) {
                row.remove(0, 1);
            }
            --width;
        }
        while (width > 0 && columnBlank(width - 1)) {
            for (QString &row : rows) {
                row.chop(1);
            }
            --width;
        }
        if (rows.isEmpty() || rows.size() > 3 || width > 3) {
            reader.fail(u"'pattern' must have 1 to 3 rows of at most 3 keys"_s);
        }
        recipe.pattern = rows;
        const QJsonObject keys = json.value(u"key"_s).toObject();
        if (keys.size() > 9) {
            reader.fail(u"'key' has more than 9 entries"_s);
        }
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            const QString item = qualifyIngredient(it.value().toString(), packNamespace);
            if (it.key().size() != 1 || it.key() == u" "_s || item.isEmpty()) {
                reader.fail(u"'key' maps single characters to item names"_s);
                break;
            }
            recipe.keys.append({it.key().at(0), item});
        }
        for (const QString &row : std::as_const(rows)) {
            for (const QChar c : row) {
                if (c != u' ' && std::ranges::none_of(recipe.keys, [c](const auto &key) { return key.first == c; })) {
                    reader.fail(u"pattern uses '%1' without a key"_s.arg(c));
                }
            }
        }
    } else {
        const QJsonArray ingredients = json.value(u"ingredients"_s).toArray();
        for (const auto &entry : ingredients) {
            const QString item = qualifyIngredient(entry.toString(), packNamespace);
            if (item.isEmpty()) {
                reader.fail(u"invalid ingredient"_s);
                break;
            }
            recipe.ingredients.append(item);
        }
        if (recipe.ingredients.isEmpty() || recipe.ingredients.size() > 9) {
            reader.fail(u"'ingredients' needs 1 to 9 items"_s);
        }
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return recipe;
}

QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"item"_s, {}};
    QcItemDefinition item;
    item.name = reader.name(u"name"_s, packNamespace);
    if (item.name.isEmpty()) {
        return qcError(reader.ok() ? u"item: 'name' is required"_s : reader.error);
    }
    reader.context = u"item '%1'"_s.arg(item.name);
    item.translationKey = reader.string(
        u"translation"_s, u"item.%1.%2"_s.arg(item.name.left(item.name.indexOf(u':')), localPart(item.name).replace(u'/', u'.')));
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
    item.fuelSeconds = reader.number(u"fuelSeconds"_s, 0.0, 0.0, maxFuelSeconds);
    item.tags = parseTags(json.value(u"tags"_s), packNamespace, reader);
    item.scoops = reader.boolean(u"scoops"_s, false);
    item.emptiesTo = reader.name(u"emptiesTo"_s, packNamespace);
    if (!item.tool.isEmpty() && item.maxStack != 1 && !json.contains(u"maxStack"_s)) {
        item.maxStack = 1;
    }
    // Burnt fuel leaves its container in the fuel slot, which has room for it only after the last item.
    if (item.fuelSeconds > 0.0 && !item.emptiesTo.isEmpty() && item.maxStack != 1) {
        reader.fail(u"fuel with 'emptiesTo' (a container) needs 'maxStack' 1"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return item;
}

QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"smelting recipe"_s, {}};
    QcSmeltingRecipe recipe;
    recipe.name = reader.name(u"name"_s, packNamespace);
    if (recipe.name.isEmpty()) {
        return qcError(reader.ok() ? u"smelting recipe: 'name' is required"_s : reader.error);
    }
    reader.context = u"smelting recipe '%1'"_s.arg(recipe.name);
    recipe.category = reader.string(u"category"_s, recipe.category);
    if (reader.ok() && !isValidCategory(recipe.category)) {
        reader.fail(invalidCategoryMessage());
    }
    recipe.inputItem = reader.name(u"input"_s, packNamespace);
    if (reader.ok() && recipe.inputItem.isEmpty()) {
        reader.fail(u"'input' is required"_s);
    }
    recipe.seconds = reader.number(u"seconds"_s, recipe.seconds, QcSmeltingRecipe::minSeconds, QcSmeltingRecipe::maxSeconds);
    const QJsonValue result = json.value(u"result"_s);
    const QJsonObject resultJson = result.toObject();
    FieldReader resultReader{resultJson, reader.context + u" result"_s, {}};
    recipe.resultItem = resultReader.name(u"item"_s, packNamespace);
    recipe.resultCount = resultReader.integer(u"count"_s, 1, 1, 64);
    if (!result.isObject() || !resultReader.ok() || recipe.resultItem.isEmpty()) {
        reader.fail(resultReader.ok() ? u"'result' needs an 'item'"_s : resultReader.error);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return recipe;
}

namespace {

std::optional<QcSoundWave> waveFromName(const QString &name)
{
    static const QHash<QString, QcSoundWave> waves = {
        {u"sine"_s, QcSoundWave::Sine},   {u"square"_s, QcSoundWave::Square}, {u"triangle"_s, QcSoundWave::Triangle},
        {u"saw"_s, QcSoundWave::Saw},     {u"noise"_s, QcSoundWave::Noise},   {u"crackle"_s, QcSoundWave::Crackle},
    };
    const auto it = waves.constFind(name);
    return it == waves.constEnd() ? std::nullopt : std::optional<QcSoundWave>(*it);
}

} // namespace

QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"sound"_s, {}};
    QcSoundDefinition sound;
    sound.name = reader.name(u"name"_s, packNamespace);
    if (sound.name.isEmpty()) {
        return qcError(reader.ok() ? u"sound: 'name' is required"_s : reader.error);
    }
    reader.context = u"sound '%1'"_s.arg(sound.name);
    const QString bus = reader.string(u"bus"_s, u"effects"_s);
    if (bus == u"ambient"_s) {
        sound.bus = QcSoundBus::Ambient;
    } else if (bus == u"music"_s) {
        sound.bus = QcSoundBus::Music;
    } else if (bus != u"effects"_s) {
        reader.fail(u"'bus' must be effects, ambient or music"_s);
    }
    sound.volume = reader.number(u"volume"_s, 1.0, 0.0, 1.0);
    sound.pitchVariation = reader.number(u"pitchVariation"_s, 0.0, 0.0, 0.5);
    sound.variants = reader.integer(u"variants"_s, 1, 1, QcSoundDefinition::maxVariants);
    sound.loop = reader.boolean(u"loop"_s, false);
    sound.range = reader.number(u"range"_s, QcSoundDefinition::defaultRange, 1.0, QcSoundDefinition::maxRange);
    const QJsonValue layers = json.value(u"layers"_s);
    if (!layers.isArray() || layers.toArray().isEmpty() || layers.toArray().size() > QcSoundDefinition::maxLayers) {
        reader.fail(u"'layers' must be an array of 1 to %1 layers"_s.arg(QcSoundDefinition::maxLayers));
    }
    const QJsonArray layerList = layers.toArray();
    for (const auto &entry : layerList) {
        if (!reader.ok()) {
            break;
        }
        if (!entry.isObject()) {
            reader.fail(u"'layers' entries must be objects"_s);
            break;
        }
        const QJsonObject layerJson = entry.toObject();
        FieldReader layerReader{layerJson, reader.context + u" layer"_s, {}};
        QcSoundLayer layer;
        const std::optional<QcSoundWave> wave = waveFromName(layerReader.string(u"wave"_s, u"noise"_s));
        if (!wave) {
            layerReader.fail(u"'wave' must be sine, square, triangle, saw, noise or crackle"_s);
        }
        layer.wave = wave.value_or(QcSoundWave::Noise);
        constexpr double low = QcSoundDefinition::minFrequency;
        constexpr double high = QcSoundDefinition::maxFrequency;
        layer.frequency = layerReader.number(u"frequency"_s, layer.frequency, low, high);
        layer.endFrequency = layerReader.number(u"endFrequency"_s, 0.0, 0.0, high);
        layer.start = layerReader.number(u"start"_s, 0.0, 0.0, QcSoundDefinition::maxLayerStart);
        layer.duration = layerReader.number(u"duration"_s, layer.duration, 0.001, QcSoundDefinition::maxLayerSeconds);
        layer.attack = layerReader.number(u"attack"_s, std::min(layer.attack, layer.duration), 0.0, layer.duration);
        layer.curve = layerReader.number(u"curve"_s, layer.curve, 0.0, 8.0);
        layer.gain = layerReader.number(u"gain"_s, 1.0, 0.0, 1.0);
        layer.lowpass = layerReader.number(u"lowpass"_s, 0.0, 0.0, high);
        layer.highpass = layerReader.number(u"highpass"_s, 0.0, 0.0, high);
        layer.tremoloRate = layerReader.number(u"tremoloRate"_s, 0.0, 0.0, 50.0);
        layer.tremoloDepth = layerReader.number(u"tremoloDepth"_s, 0.0, 0.0, 1.0);
        layer.density = layerReader.number(u"density"_s, layer.density, 1.0, 5000.0);
        if (!layerReader.ok()) {
            reader.fail(layerReader.error);
        }
        sound.layers.append(layer);
    }
    if (reader.ok()) {
        // Filter and sweep frequencies of 0 mean "off"; other values must be audible.
        if (const QString problem = sound.problem(); !problem.isEmpty()) {
            reader.fail(problem);
        }
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return sound;
}

QcResult<QcBiomeDefinition> parseBiome(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"biome"_s, {}};
    QcBiomeDefinition biome;
    biome.name = reader.name(u"name"_s, packNamespace);
    if (biome.name.isEmpty()) {
        return qcError(reader.ok() ? u"biome: 'name' is required"_s : reader.error);
    }
    reader.context = u"biome '%1'"_s.arg(biome.name);
    biome.translationKey = reader.string(
        u"translation"_s, u"biome.%1.%2"_s.arg(biome.name.left(biome.name.indexOf(u':')), localPart(biome.name)));
    biome.descriptionKey = reader.string(u"description"_s, biome.translationKey + u".description"_s);
    biome.temperature = reader.number(u"temperature"_s, 0.5, 0.0, 1.0);
    biome.humidity = reader.number(u"humidity"_s, 0.5, 0.0, 1.0);
    biome.ruggedness = reader.number(u"ruggedness"_s, biome.ruggedness, 0.0, 1.0);
    biome.baseHeight = reader.number(u"baseHeight"_s, 68.0, 1.0, QcWorld::worldHeight - 40.0);
    biome.heightVariation = reader.number(u"heightVariation"_s, 6.0, 0.0, 120.0);
    biome.ridges = reader.number(u"ridges"_s, 0.0, 0.0, 1.0);
    biome.terraceHeight = reader.integer(u"terraceHeight"_s, 0, 0, 32);
    biome.overhang = reader.integer(u"overhang"_s, 0, 0, 16);
    biome.steepBlock = reader.name(u"steep"_s, packNamespace);
    biome.steepSlope = reader.integer(u"steepSlope"_s, biome.steepSlope, 1, 32);
    biome.surfaceBlock = reader.name(u"surface"_s, packNamespace);
    biome.subsurfaceBlock = reader.name(u"subsurface"_s, packNamespace, biome.surfaceBlock);
    biome.subsurfaceDepth = reader.integer(u"subsurfaceDepth"_s, 3, 0, 16);
    biome.underwaterBlock = reader.name(u"underwater"_s, packNamespace, biome.subsurfaceBlock);
    biome.beachBlock = reader.name(u"beach"_s, packNamespace, biome.surfaceBlock);
    biome.frozenWaterBlock = reader.name(u"frozenWater"_s, packNamespace);
    biome.snowLine = reader.integer(u"snowLine"_s, 0, 0, QcWorld::worldHeight);
    biome.snowBlock = reader.name(u"snow"_s, packNamespace);
    if (biome.surfaceBlock.isEmpty()) {
        reader.fail(u"'surface' is required"_s);
    }
    if (biome.snowLine > 0 && biome.snowBlock.isEmpty()) {
        reader.fail(u"'snowLine' needs a 'snow' block"_s);
    }
    const QJsonValue features = json.value(u"features"_s);
    if (!features.isUndefined() && !features.isArray()) {
        reader.fail(u"'features' must be an array"_s);
    }
    const QJsonArray featureList = features.toArray();
    if (featureList.size() > 64) {
        reader.fail(u"'features' has more than 64 entries"_s);
    }
    for (const auto &entry : featureList) {
        const QJsonObject placementJson = entry.toObject();
        FieldReader placementReader{placementJson, reader.context, {}};
        const QcFeaturePlacement placement{placementReader.name(u"feature"_s, packNamespace),
                                           placementReader.number(u"density"_s, 0.0, 0.0, 1.0)};
        if (!entry.isObject() || placement.feature.isEmpty() || !placementReader.ok()) {
            reader.fail(placementReader.ok() ? u"feature entries need 'feature' and 'density'"_s : placementReader.error);
            break;
        }
        biome.features.append(placement);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return biome;
}

QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"feature"_s, {}};
    QcFeatureDefinition feature;
    feature.name = reader.name(u"name"_s, packNamespace);
    if (feature.name.isEmpty()) {
        return qcError(reader.ok() ? u"feature: 'name' is required"_s : reader.error);
    }
    reader.context = u"feature '%1'"_s.arg(feature.name);
    const QString type = reader.string(u"type"_s, u"tree"_s);
    if (type == u"tree"_s) {
        feature.type = QcFeatureDefinition::Type::Tree;
    } else if (type == u"column"_s) {
        feature.type = QcFeatureDefinition::Type::Column;
    } else if (type == u"plant"_s) {
        feature.type = QcFeatureDefinition::Type::Plant;
    } else if (type == u"pool"_s) {
        feature.type = QcFeatureDefinition::Type::Pool;
    } else {
        reader.fail(u"'type' must be tree, column, plant or pool"_s);
    }
    const bool singleBlock = feature.type == QcFeatureDefinition::Type::Plant || feature.type == QcFeatureDefinition::Type::Pool;
    feature.trunkBlock = reader.name(singleBlock ? u"block"_s : u"trunk"_s, packNamespace);
    if (feature.type == QcFeatureDefinition::Type::Pool) {
        feature.minY = reader.integer(u"minY"_s, feature.minY, 1, QcWorld::worldHeight - 2);
        feature.maxY = reader.integer(u"maxY"_s, std::max(feature.minY, feature.maxY), feature.minY, QcWorld::worldHeight - 2);
        feature.attempts = reader.integer(u"attempts"_s, feature.attempts, 1, maxPoolAttempts);
        feature.chance = reader.number(u"chance"_s, feature.chance, 0.0, 1.0);
        feature.radius = reader.integer(u"radius"_s, feature.radius, 1, 6);
        feature.depth = reader.integer(u"depth"_s, feature.depth, 1, 4);
        feature.surface = reader.boolean(u"surface"_s, false);
        const QJsonValue biomes = json.value(u"biomes"_s);
        if (!biomes.isUndefined() && (!biomes.isArray() || biomes.toArray().size() > 64)) {
            reader.fail(u"'biomes' must be an array of at most 64 biome names"_s);
        }
        const QJsonArray biomeList = biomes.toArray();
        for (const auto &entry : biomeList) {
            const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
            if (name.isEmpty()) {
                reader.fail(u"invalid 'biomes' entry"_s);
                break;
            }
            feature.biomes.append(name);
        }
    }
    feature.nearWater = reader.boolean(u"nearWater"_s, false);
    feature.patchSize = reader.integer(u"patchSize"_s, 0, 0, 512);
    feature.leavesBlock = reader.name(u"leaves"_s, packNamespace);
    feature.minHeight = reader.integer(u"minHeight"_s, 4, 1, 32);
    feature.maxHeight = reader.integer(u"maxHeight"_s, feature.minHeight, feature.minHeight, 32);
    feature.shape = reader.string(u"shape"_s, u"round"_s);
    if (feature.shape != u"round"_s && feature.shape != u"cone"_s) {
        reader.fail(u"'shape' must be round or cone"_s);
    }
    if (feature.trunkBlock.isEmpty()) {
        reader.fail(singleBlock ? u"'block' is required"_s : u"'trunk' is required"_s);
    }
    if (feature.type == QcFeatureDefinition::Type::Tree && feature.leavesBlock.isEmpty()) {
        reader.fail(u"trees need 'leaves'"_s);
    }
    const QJsonArray ground = json.value(u"ground"_s).toArray();
    for (const auto &entry : ground) {
        const QString name = QcContentNames::qualify(entry.toString(), packNamespace);
        if (name.isEmpty()) {
            reader.fail(u"invalid 'ground' entry"_s);
            break;
        }
        feature.groundBlocks.append(name);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return feature;
}

QcResult<QcOreDefinition> parseOre(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"ore"_s, {}};
    QcOreDefinition ore;
    ore.name = reader.name(u"name"_s, packNamespace);
    if (ore.name.isEmpty()) {
        return qcError(reader.ok() ? u"ore: 'name' is required"_s : reader.error);
    }
    reader.context = u"ore '%1'"_s.arg(ore.name);
    ore.block = reader.name(u"block"_s, packNamespace);
    // Empty means the terrain's stone block.
    ore.replaces = reader.name(u"replaces"_s, packNamespace);
    ore.minY = reader.integer(u"minY"_s, 1, 0, QcWorld::worldHeight - 1);
    ore.maxY = reader.integer(u"maxY"_s, 64, ore.minY, QcWorld::worldHeight - 1);
    ore.veinsPerChunk = reader.integer(u"veinsPerChunk"_s, 8, 0, 256);
    ore.veinSize = reader.integer(u"veinSize"_s, 6, 1, 64);
    if (ore.block.isEmpty()) {
        reader.fail(u"'block' is required"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return ore;
}

QcResult<QcTerrainSettings> parseTerrainSettings(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"terrain"_s, {}};
    QcTerrainSettings settings;
    settings.seaLevel = reader.integer(u"seaLevel"_s, QcWorld::seaLevel, 8, QcWorld::worldHeight - 32);
    settings.stoneBlock = reader.name(u"stone"_s, packNamespace);
    settings.waterBlock = reader.name(u"water"_s, packNamespace);
    settings.bedrockBlock = reader.name(u"bedrock"_s, packNamespace);
    settings.caves = reader.boolean(u"caves"_s, true);
    settings.caveTunnelWidth = reader.number(u"caveTunnelWidth"_s, settings.caveTunnelWidth, 0.0, 0.5);
    settings.caveCavernThreshold = reader.number(u"caveCavernThreshold"_s, settings.caveCavernThreshold, 0.0, 1.0);
    settings.caveLiquidBlock = reader.name(u"caveLiquid"_s, packNamespace);
    settings.caveLiquidLevel = reader.integer(u"caveLiquidLevel"_s, settings.caveLiquidLevel, 1, QcWorld::worldHeight - 1);
    settings.caveRoof = reader.integer(u"caveRoof"_s, settings.caveRoof, 0, 32);
    settings.caveEntrances = reader.number(u"caveEntrances"_s, settings.caveEntrances, 0.0, 1.0);
    settings.bedrockLayers = reader.integer(u"bedrockLayers"_s, settings.bedrockLayers, 1, 16);
    if (settings.stoneBlock.isEmpty() || settings.waterBlock.isEmpty()) {
        reader.fail(u"'stone' and 'water' are required"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"handbook chapter"_s, {}};
    QcHandbookChapter chapter;
    chapter.name = reader.name(u"name"_s, packNamespace);
    if (chapter.name.isEmpty()) {
        return qcError(reader.ok() ? u"handbook chapter: 'name' is required"_s : reader.error);
    }
    reader.context = u"handbook chapter '%1'"_s.arg(chapter.name);
    chapter.titleKey = reader.string(
        u"title"_s, u"chapter.%1.%2"_s.arg(chapter.name.left(chapter.name.indexOf(u':')), localPart(chapter.name).replace(u'/', u'.')));
    chapter.order = reader.integer(u"order"_s, chapter.order, -1000000, 1000000);
    chapter.iconItem = reader.name(u"icon"_s, packNamespace);
    static const QRegularExpression sectionPattern(u"^[a-z0-9_]{1,32}$"_s);
    const QJsonValue sections = json.value(u"sections"_s);
    if (!sections.isArray() || sections.toArray().isEmpty() || sections.toArray().size() > QcHandbookChapter::maxSections) {
        reader.fail(u"'sections' must be an array of 1..%1 section names"_s.arg(QcHandbookChapter::maxSections));
    }
    const QJsonArray sectionList = sections.toArray();
    for (const auto &section : sectionList) {
        if (!section.isString() || !sectionPattern.match(section.toString()).hasMatch() || chapter.sections.contains(section.toString())) {
            reader.fail(u"section names are distinct lower-case words"_s);
            break;
        }
        chapter.sections.append(section.toString());
    }
    const QJsonValue related = json.value(u"related"_s);
    if (!related.isUndefined() && (!related.isArray() || related.toArray().size() > QcHandbookChapter::maxRelated)) {
        reader.fail(u"'related' must be an array of at most %1 names"_s.arg(QcHandbookChapter::maxRelated));
    }
    const QJsonArray relatedList = related.toArray();
    for (const auto &name : relatedList) {
        const QString qualified = name.isString() ? QcContentNames::qualify(name.toString(), packNamespace) : QString();
        if (qualified.isEmpty()) {
            reader.fail(u"'related' entries must be content names"_s);
            break;
        }
        chapter.related.append(qualified);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return chapter;
}

} // namespace QcContentParser
