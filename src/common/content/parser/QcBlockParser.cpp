#include "common/content/parser/QcBlockParser.h"

#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/parser/QcBlastParser.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcFarmingParser.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcShapeParser.h"
#include "common/content/parser/QcSignalParser.h"
#include "common/content/registry/QcBlockStateTable.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr double minMachineSpeed = 0.01;
constexpr double maxMachineSpeed = 100.0;

[[nodiscard]] std::optional<QcRenderLayer> renderLayerFromName(const QString &name)
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

bool isCross(const QcBlockDefinition &block)
{
    return block.shapeName == u"qtcraft:cross"_s;
}


// Whether the block has the state property `name` with all of `values`.
bool hasValues(const QcBlockDefinition &block, const QString &name, const QStringList &values)
{
    const auto property = std::ranges::find(block.properties, name, &QcBlockProperty::name);
    return property != block.properties.cend() && std::ranges::all_of(values, [&](const QString &value) { return property->values.contains(value); });
}

[[nodiscard]] QList<QcLiquidReaction> parseLiquidReactions(const QJsonObject &flowJson, QcJsonFieldReader &flowReader,
                                                           const QString &packNamespace)
{
    const QJsonValue reactions = flowJson.value(u"reactions"_s);
    if (!reactions.isUndefined() && (!reactions.isArray() || reactions.toArray().size() > 16)) {
        flowReader.fail(u"'reactions' must be an array of at most 16 entries"_s);
    }

    QList<QcLiquidReaction> result;
    const QJsonArray reactionList = reactions.toArray();
    for (const auto &entry : reactionList) {
        const QJsonObject reactionJson = entry.toObject();
        QcJsonFieldReader reactionReader{reactionJson, flowReader.context, {}};
        const QcLiquidReaction reaction{reactionReader.name(u"with"_s, packNamespace), reactionReader.name(u"source"_s, packNamespace),
                                        reactionReader.name(u"flowing"_s, packNamespace)};
        if (!entry.isObject() || !reactionReader.ok() || reaction.with.isEmpty() || reaction.source.isEmpty()
            || reaction.flowing.isEmpty()) {
            flowReader.fail(reactionReader.ok() ? u"reactions need 'with', 'source' and 'flowing'"_s : reactionReader.error);
            break;
        }
        result.append(reaction);
    }
    return result;
}

// The flow keeps its state in `level` ("0" source, then one value per block of distance) and
// optionally `falling`.
void checkLiquidStates(const QcBlockDefinition &block, QcJsonFieldReader &flowReader)
{
    const QcLiquidDefinition &liquid = block.flow;
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
}

// A rail needs the states its track is made of (decision E-382): the direction it faces and its
// shape; a rail without the shape `curve` never bends (decision E-384). A rail that acts on carts
// needs its `powered` state and never bends.
void checkRailStates(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const QcBlockProperty *facing = block.property(u"facing"_s);
    const QcBlockProperty *shape = block.property(u"shape"_s);
    const QStringList sides{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    const QStringList shapes{u"straight"_s, u"ascending"_s};
    const bool facingOk = facing && std::ranges::all_of(sides, [facing](const QString &side) { return facing->values.contains(side); });
    const bool shapeOk = shape && std::ranges::all_of(shapes, [shape](const QString &value) { return shape->values.contains(value); });
    if (!facingOk || !shapeOk) {
        reader.fail(u"a rail needs the states 'facing' (north, east, south, west) and 'shape' (straight, ascending and optionally curve)"_s);
    }
    if (block.solid || block.liquid) {
        reader.fail(u"a rail can be neither solid nor a liquid"_s);
    }
    const QcBlockProperty *powered = block.property(u"powered"_s);
    const bool poweredOk = powered && powered->values.contains(u"true"_s) && powered->values.contains(u"false"_s);
    if (block.railAction != QcRailAction::None && !poweredOk) {
        reader.fail(u"a rail with 'railAction' needs the state 'powered' (true, false)"_s);
    }
    // Power runs along a straight line of rails (QcRails::lineOf); a ring of curves shorter than the
    // line it reads would be cut at an arbitrary place.
    if (block.railAction != QcRailAction::None && shape && shape->values.contains(u"curve"_s)) {
        reader.fail(u"a rail with 'railAction' cannot have the shape 'curve'"_s);
    }
}

void checkPartTextures(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    for (const QcShapePart &part : std::as_const(block.shape.parts)) {
        if (!part.texture.isEmpty() && block.slotTexture(part.texture).isEmpty() && block.renderLayer != QcRenderLayer::Invisible) {
            reader.fail(u"shape part uses the unknown texture slot '%1'"_s.arg(part.texture));
            break;
        }
    }
}

// Reads one block definition: owns the definition being built and the reader of the block's JSON
// object. The parts run in a fixed order, later parts check fields that earlier parts read.
class BlockReader
{
public:
    BlockReader(const QJsonObject &json, const QString &packNamespace)
        : m_json(json)
        , m_namespace(packNamespace)
        , m_reader{json, u"block"_s, {}}
    {
    }

    [[nodiscard]] QcResult<QcBlockDefinition> read();

private:
    void readNamedObjects();
    void readShapeField();
    void readRenderFields();
    void readStates();
    void readPlacementFields();
    void readConnects(const QJsonObject &connectJson, QcJsonFieldReader &connectReader);
    void readContainer(QcJsonFieldReader &containerReader);
    void readMiningFields();
    void readInfested(QcJsonFieldReader &infestedReader);
    void readSpawner(QcJsonFieldReader &spawnerReader);
    void readMachine(QcJsonFieldReader &machineReader);
    void readDrops();
    void readSapling(const QJsonObject &saplingJson, QcJsonFieldReader &saplingReader);
    void readFlow();
    void readRailAction();
    void readBehaviourFields();
    void readFireFields();
    void readMovementFields();
    void readGround();
    void readGrowth(QcJsonFieldReader &growthReader);
    void readFarming();
    void readSounds(QcJsonFieldReader &soundReader);
    void readTextureSlots(const QJsonObject &faces, QcJsonFieldReader &textureReader);
    void readTextures();

    const QJsonObject &m_json;
    const QString &m_namespace;
    QcJsonFieldReader m_reader;
    QcBlockDefinition m_block;
};

// "cube" and "cross" always name the base shapes; other names resolve in the pack's namespace.
void BlockReader::readShapeField()
{
    const QJsonValue shapeValue = m_json.value(u"shape"_s);
    if (shapeValue.isObject()) {
        const QJsonObject inlineShape = shapeValue.toObject();
        QcJsonFieldReader shapeReader{inlineShape, m_reader.context + u" shape"_s, {}};
        m_block.shape = QcShapeParser::parseShapeBody(inlineShape, shapeReader);
        m_reader.adopt(shapeReader);
    } else if (shapeValue.isString()) {
        const QString shapeName = shapeValue.toString();
        if (shapeName == u"cross"_s || shapeName == u"cube"_s) {
            m_block.shapeName = u"qtcraft:"_s + shapeName;
        } else {
            m_block.shapeName = m_reader.name(u"shape"_s, m_namespace);
        }
        if (m_block.shapeName == u"qtcraft:cube"_s) {
            m_block.shapeName.clear();
        }
    } else if (!shapeValue.isUndefined()) {
        m_reader.fail(u"'shape' must be a shape name or an inline shape object"_s);
    }
}

void BlockReader::readRenderFields()
{
    const bool cross = isCross(m_block);
    const QString layerName = m_reader.string(u"render"_s, cross ? u"cutout"_s : u"opaque"_s);
    const std::optional<QcRenderLayer> layer = renderLayerFromName(layerName);
    if (!layer) {
        m_reader.fail(u"'render' must be opaque, cutout, translucent or invisible"_s);
    }

    m_block.renderLayer = layer.value_or(QcRenderLayer::Opaque);
    const bool fullCube = m_block.renderLayer == QcRenderLayer::Opaque && !cross;
    m_block.solid = m_reader.boolean(u"solid"_s, !cross);
    m_block.opaque = m_reader.boolean(u"opaque"_s, fullCube);
    if (cross && (m_block.opaque || m_block.renderLayer == QcRenderLayer::Opaque)) {
        m_reader.fail(u"cross-shaped blocks must use render cutout or translucent and cannot be opaque"_s);
    }

    m_block.liquid = m_reader.boolean(u"liquid"_s, false);
    m_block.replaceable = m_reader.boolean(u"replaceable"_s, false);
}

void BlockReader::readStates()
{
    const QJsonValue states = m_json.value(u"states"_s);
    if (!states.isUndefined() && (!states.isObject() || states.toObject().size() > QcBlockProperty::maxProperties)) {
        m_reader.fail(u"'states' must be an object of at most %1 properties"_s.arg(QcBlockProperty::maxProperties));
    }

    const QJsonObject stateJson = states.toObject();
    for (auto it = stateJson.constBegin(); it != stateJson.constEnd(); ++it) {
        QcBlockProperty property;
        property.name = it.key();
        const QJsonArray values = it.value().toArray();
        for (const auto &value : values) {
            const std::optional<QString> text = QcContentFields::stateValue(value);
            if (!text) {
                m_reader.fail(u"state values must be strings or booleans"_s);
                break;
            }
            property.values.append(*text);
        }
        m_block.properties.append(std::move(property));
    }

    // The first listed value is the default; JSON keeps the order of array entries.
    if (const QString problem = QcBlockStateTable::problem(m_block); m_reader.ok() && !problem.isEmpty()) {
        m_reader.fail(problem);
    }
}

void BlockReader::readPlacementFields()
{
    m_block.lightLevel = m_reader.integer(u"light"_s, 0, 0, QcBlockDefinition::maxLightLevel);
    m_block.climbable = m_reader.boolean(u"climbable"_s, false);
    m_block.toggles = m_reader.boolean(u"toggles"_s, false);
    m_block.door = m_reader.boolean(u"door"_s, false);
    m_block.bed = m_reader.boolean(u"bed"_s, false);
    // A bed is placed as two halves along its facing: without both it could never be placed.
    if (m_block.bed && (!hasValues(m_block, u"part"_s, {u"foot"_s, u"head"_s})
                        || !hasValues(m_block, u"facing"_s, {u"north"_s, u"east"_s, u"south"_s, u"west"_s}))) {
        m_reader.fail(u"a bed needs the states 'part' (foot, head) and 'facing' (north, east, south, west)"_s);
    }

    const QString facing = m_reader.string(u"facing"_s, u"away"_s);
    if (facing == u"toward"_s) {
        m_block.facingRule = QcFacingRule::Toward;
    } else if (facing == u"into"_s) {
        m_block.facingRule = QcFacingRule::Into;
    } else if (facing != u"away"_s) {
        m_reader.fail(u"'facing' must be away, toward or into"_s);
    }

    const QString support = m_reader.string(u"support"_s, isCross(m_block) ? u"below"_s : u"none"_s);
    if (support == u"below"_s) {
        m_block.support = QcBlockSupport::Below;
    } else if (support == u"attached"_s) {
        m_block.support = QcBlockSupport::Attached;
    } else if (support != u"none"_s) {
        m_reader.fail(u"'support' must be none, below or attached"_s);
    }
}

void BlockReader::readConnects(const QJsonObject &connectJson, QcJsonFieldReader &connectReader)
{
    m_block.connectGroup = connectReader.string(u"group"_s);

    const QJsonArray targets = connectJson.value(u"to"_s).toArray();
    for (const auto &target : targets) {
        if (!target.isString() || target.toString().isEmpty() || target.toString().size() > 64 || m_block.connectsTo.size() >= 16) {
            connectReader.fail(u"'to' must list at most 16 group names"_s);
            break;
        }
        m_block.connectsTo.append(target.toString());
    }

    if (m_block.connectsTo.isEmpty() && !m_block.connectGroup.isEmpty()) {
        m_block.connectsTo.append(m_block.connectGroup);
    }
    if (m_block.connectGroup.size() > 64) {
        connectReader.fail(u"'group' is too long"_s);
    }
}

void BlockReader::readContainer(QcJsonFieldReader &containerReader)
{
    m_block.containerSlots = containerReader.integer(u"slots"_s, 27, 1, QcBlockDefinition::maxContainerSlots);
    m_block.containerColumns = containerReader.integer(u"columns"_s, 9, 1, 9);
    if (containerReader.ok() && (m_block.opensCrafting || m_json.contains(u"machine"_s))) {
        containerReader.fail(u"a block cannot store items and also be a crafting table or machine"_s);
    }
}

void BlockReader::readMiningFields()
{
    m_block.lightOpacity = m_reader.integer(u"lightOpacity"_s, m_block.opaque ? 15 : (m_block.liquid ? 2 : 0), 0, 15);
    m_block.hardness = m_reader.number(u"hardness"_s, 1.0, -1.0, 1000.0);
    m_block.tool = m_reader.string(u"tool"_s);
    m_block.toolLevel = m_reader.integer(u"toolLevel"_s, 0, 0, 100);
    m_block.opensCrafting = m_reader.boolean(u"crafting"_s, false);
    m_block.fuelSeconds = m_reader.number(u"fuelSeconds"_s, 0.0, 0.0, QcContentFields::maxFuelSeconds);
}

// An infested block (decision E-453): the block it stands in for and the creature inside.
void BlockReader::readInfested(QcJsonFieldReader &infestedReader)
{
    m_block.infestsBlock = infestedReader.name(u"block"_s, m_namespace);
    m_block.infestsMob = infestedReader.name(u"mob"_s, m_namespace);
    if (infestedReader.ok() && (m_block.infestsBlock.isEmpty() || m_block.infestsMob.isEmpty())) {
        infestedReader.fail(u"needs 'block' and 'mob'"_s);
    }
}

// A monster spawner (decision E-520): its rules; the area's sizes are odd so it centres on the block.
void BlockReader::readSpawner(QcJsonFieldReader &spawnerReader)
{
    using Spawner = QcSpawnerDefinition;
    Spawner spawner;
    spawner.isSpawner = true;
    spawner.mob = spawnerReader.name(u"mob"_s, m_namespace);
    spawner.range = spawnerReader.number(u"range"_s, spawner.range, 1.0, Spawner::maxRange);
    spawner.maxNearby = spawnerReader.integer(u"maxNearby"_s, spawner.maxNearby, 1, Spawner::maxNearbyLimit);
    spawner.nearbyRange = spawnerReader.integer(u"nearbyRange"_s, spawner.nearbyRange, 0, Spawner::maxArea);
    spawner.count = spawnerReader.integer(u"count"_s, spawner.count, 1, Spawner::maxCount);
    spawner.minDelay = spawnerReader.number(u"minDelay"_s, spawner.minDelay, 0.05, Spawner::maxDelaySeconds);
    spawner.maxDelay = spawnerReader.number(u"maxDelay"_s, spawner.maxDelay, spawner.minDelay, Spawner::maxDelaySeconds);
    spawner.areaX = spawnerReader.integer(u"areaX"_s, spawner.areaX, 1, Spawner::maxArea);
    spawner.areaY = spawnerReader.integer(u"areaY"_s, spawner.areaY, 1, Spawner::maxArea);
    spawner.areaZ = spawnerReader.integer(u"areaZ"_s, spawner.areaZ, 1, Spawner::maxArea);
    spawner.maxLight = spawnerReader.integer(u"maxLight"_s, spawner.maxLight, 0, QcBlockDefinition::maxLightLevel);
    if (spawnerReader.ok() && (spawner.areaX % 2 == 0 || spawner.areaY % 2 == 0 || spawner.areaZ % 2 == 0)) {
        spawnerReader.fail(u"the spawn area's sizes must be odd"_s);
    }
    m_block.spawner = std::move(spawner);
}

void BlockReader::readMachine(QcJsonFieldReader &machineReader)
{
    m_block.machine.category = machineReader.string(u"category"_s, QString::fromLatin1(QcSmeltingRecipe::defaultCategory));
    m_block.machine.speed = machineReader.number(u"speed"_s, 1.0, minMachineSpeed, maxMachineSpeed);
    m_block.machine.sound = machineReader.name(u"sound"_s, m_namespace, QString::fromLatin1(QcSoundKeys::machineActive));

    if (machineReader.ok() && !QcContentFields::isValidCategory(m_block.machine.category)) {
        machineReader.fail(QcContentFields::invalidCategoryMessage());
    }
    if (machineReader.ok() && m_block.opensCrafting) {
        machineReader.fail(u"a block cannot be a crafting table and a machine"_s);
    }
}

void BlockReader::readDrops()
{
    const QJsonValue drops = m_json.value(u"drops"_s);
    if (drops.isUndefined()) {
        m_block.drops = {{m_block.name, 1}};
        return;
    }

    if (!drops.isArray() || drops.toArray().size() > 64) {
        m_reader.fail(u"'drops' must be an array of at most 64 entries"_s);
        return;
    }

    const QJsonArray dropList = drops.toArray();
    for (const auto &entry : dropList) {
        if (!entry.isObject()) {
            m_reader.fail(u"'drops' entries must be objects"_s);
            break;
        }
        const QJsonObject dropJson = entry.toObject();
        QcJsonFieldReader dropReader{dropJson, m_reader.context, {}};
        const QcItemDrop drop{dropReader.name(u"item"_s, m_namespace), dropReader.integer(u"count"_s, 1, 1, 64),
                              dropReader.number(u"chance"_s, 1.0, 0.0, 1.0), QcShapeParser::parseConditions(dropJson, dropReader),
                              dropReader.integer(u"maxCount"_s, 0, 0, 64)};
        if (!dropReader.ok() || drop.item.isEmpty()) {
            m_reader.fail(dropReader.ok() ? u"drop needs an 'item'"_s : dropReader.error);
            break;
        }
        // Conditions name states of the block itself (decision E-289).
        for (const QcShapeCondition &condition : drop.when) {
            const QcBlockProperty *property = m_block.property(condition.property);
            const bool known = property && std::ranges::all_of(condition.values, [property](const QString &value) {
                                   return property->values.contains(value);
                               });
            if (!known) {
                m_reader.fail(u"drop 'when' names an unknown state '%1'"_s.arg(condition.property));
            }
        }
        m_block.drops.append(drop);
    }
}

void BlockReader::readSapling(const QJsonObject &saplingJson, QcJsonFieldReader &saplingReader)
{
    QcSaplingDefinition sapling;
    sapling.feature = saplingReader.name(u"feature"_s, m_namespace);
    sapling.minSeconds = saplingReader.number(u"minSeconds"_s, 120.0, 1.0, 86400.0);
    sapling.maxSeconds = saplingReader.number(u"maxSeconds"_s, std::max(300.0, sapling.minSeconds), sapling.minSeconds, 86400.0);
    sapling.plantChance = saplingReader.number(u"plantChance"_s, 0.0, 0.0, 1.0);

    const QJsonValue plantOn = saplingJson.value(u"plantOn"_s);
    if (!plantOn.isUndefined() && (!plantOn.isArray() || plantOn.toArray().size() > 64)) {
        saplingReader.fail(u"'plantOn' must be an array of at most 64 block names"_s);
    }

    const QJsonArray plantOnList = plantOn.toArray();
    for (const auto &entry : plantOnList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), m_namespace) : QString();
        if (name.isEmpty()) {
            saplingReader.fail(u"invalid 'plantOn' entry"_s);
            break;
        }
        sapling.plantOn.append(name);
    }

    if (saplingReader.ok() && sapling.feature.isEmpty()) {
        saplingReader.fail(u"'feature' is required"_s);
    }
    if (saplingReader.ok() && m_block.solid) {
        saplingReader.fail(u"saplings must not be solid"_s);
    }
    m_block.sapling = std::move(sapling);
}

void BlockReader::readFlow()
{
    const QJsonValue flow = m_json.value(u"flow"_s);
    if (!flow.isUndefined() && (!flow.isObject() || !m_block.liquid)) {
        m_reader.fail(u"'flow' must be an object and needs 'liquid': true"_s);
        return;
    }
    if (!flow.isObject()) {
        return;
    }

    const QJsonObject flowJson = flow.toObject();
    QcJsonFieldReader flowReader{flowJson, m_reader.context + u" flow"_s, {}};
    QcLiquidDefinition liquid;
    liquid.distance = flowReader.integer(u"distance"_s, 7, 1, QcLiquidDefinition::maxDistance);
    liquid.ticks = flowReader.integer(u"ticks"_s, 5, 1, QcLiquidDefinition::maxTicks);
    liquid.infinite = flowReader.boolean(u"infinite"_s, false);
    liquid.dropSearch = flowReader.integer(u"dropSearch"_s, 4, 0, QcLiquidDefinition::maxDropSearch);
    liquid.movementFactor = flowReader.number(u"movement"_s, 1.0, 0.05, 2.0);
    liquid.bucket = flowReader.name(u"bucket"_s, m_namespace);
    liquid.bottle = flowReader.name(u"bottle"_s, m_namespace);
    liquid.sound = flowReader.name(u"sound"_s, m_namespace);
    liquid.reactions = parseLiquidReactions(flowJson, flowReader, m_namespace);
    m_block.flow = std::move(liquid);

    checkLiquidStates(m_block, flowReader);
    m_reader.adopt(flowReader);
}

void BlockReader::readRailAction()
{
    const QString action = m_reader.string(u"railAction"_s, u"none"_s);
    if (action == u"boost"_s) {
        m_block.railAction = QcRailAction::Boost;
    } else if (action == u"activate"_s) {
        m_block.railAction = QcRailAction::Activate;
    } else if (action != u"none"_s) {
        m_reader.fail(u"'railAction' must be none, boost or activate"_s);
    }
    if (m_block.railAction != QcRailAction::None && !m_block.rail) {
        m_reader.fail(u"only a rail has a 'railAction'"_s);
    }
}

void BlockReader::readBehaviourFields()
{
    m_block.contactDamage = m_reader.number(u"contactDamage"_s, 0.0, 0.0, 100.0);
    m_block.washable = m_reader.boolean(u"washable"_s, isCross(m_block));
    m_block.falls = m_reader.boolean(u"falls"_s, false);
    if (m_block.falls && (m_block.liquid || !m_block.solid)) {
        m_reader.fail(u"only solid blocks can fall"_s);
    }
    m_block.rail = m_reader.boolean(u"rail"_s, false);
    readRailAction();
    m_block.supportsAbove = m_reader.boolean(u"supportsAbove"_s, false);
    if (m_block.rail) {
        checkRailStates(m_block, m_reader);
    }
}

// Fire, flammability and eternal fire (decision E-392).
void BlockReader::readFireFields()
{
    QcFireDefinition fire;
    fire.isFire = m_reader.boolean(u"fire"_s, false);
    fire.eternal = m_reader.boolean(u"eternalFire"_s, false);
    fire.setsAlightSeconds = m_reader.integer(u"setsAlight"_s, 0, 0, QcFireDefinition::maxAlightSeconds);
    fire.sparks = m_reader.integer(u"sparks"_s, 0, 0, QcFireDefinition::maxChance);
    m_reader.nestedObject(u"flammable"_s, [&fire](const QJsonObject &, QcJsonFieldReader &nested) {
        fire.ignite = nested.integer(u"ignite"_s, 0, 0, QcFireDefinition::maxChance);
        fire.burn = nested.integer(u"burn"_s, 0, 0, QcFireDefinition::maxChance);
    });
    m_block.fire = fire;

    if (fire.isFire && (m_block.solid || m_block.liquid || fire.isFlammable() || fire.eternal)) {
        m_reader.fail(u"fire must be neither solid nor liquid, flammable or eternal"_s);
    }
    if (fire.isFire && m_block.lightOpacity != 0) {
        m_reader.fail(u"fire must have 'lightOpacity' 0"_s);
    }
    if (m_block.liquid && fire.isFlammable()) {
        m_reader.fail(u"a liquid cannot be flammable"_s);
    }
    if (fire.setsAlightSeconds > 0 && m_block.solid) {
        m_reader.fail(u"'setsAlight' needs a block bodies can stand in (not solid)"_s);
    }
}

// Slowing inside (cobweb), the walking surface (mud, ice) and hardening next to another block
// (concrete powder), decisions E-207 and E-208.
void BlockReader::readMovementFields()
{
    m_block.slowsInside = m_reader.number(u"slowsInside"_s, 1.0, 0.01, 1.0);
    if (m_block.slowsInside < 1.0 && (m_block.solid || m_block.liquid)) {
        m_reader.fail(u"only blocks that are neither solid nor liquid can slow bodies inside"_s);
    }

    m_reader.nestedObject(u"surface"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_block.surfaceSpeed = nested.number(u"speed"_s, 1.0, 0.1, 2.0);
        m_block.surfaceJump = nested.number(u"jump"_s, 1.0, 0.1, 2.0);
        m_block.slipperiness = nested.number(u"slipperiness"_s, QcBlockDefinition::grippingSlipperiness,
                                             QcBlockDefinition::grippingSlipperiness, QcBlockDefinition::maxSlipperiness);
    });
    m_reader.nestedObject(u"hardens"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_block.hardensWith = nested.name(u"with"_s, m_namespace);
        m_block.hardensInto = nested.name(u"into"_s, m_namespace);
        if (nested.ok() && (m_block.hardensWith.isEmpty() || m_block.hardensInto.isEmpty())) {
            nested.fail(u"needs 'with' and 'into'"_s);
        }
        if (nested.ok() && (m_block.hardensInto == m_block.name || m_block.hardensWith == m_block.name)) {
            nested.fail(u"a block cannot harden into or next to itself"_s);
        }
    });
}

void BlockReader::readGround()
{
    const QJsonValue ground = m_json.value(u"ground"_s);
    if (!ground.isUndefined() && (!ground.isArray() || ground.toArray().size() > 64)) {
        m_reader.fail(u"'ground' must be an array of at most 64 block names"_s);
    }

    const QJsonArray groundList = ground.toArray();
    for (const auto &entry : groundList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), m_namespace) : QString();
        if (name.isEmpty()) {
            m_reader.fail(u"invalid 'ground' entry"_s);
            break;
        }
        m_block.groundBlocks.append(name);
    }

    m_block.groundLiquid = m_reader.name(u"groundLiquid"_s, m_namespace);
    if ((!m_block.groundBlocks.isEmpty() || !m_block.groundLiquid.isEmpty()) && m_block.support != QcBlockSupport::Below) {
        m_reader.fail(u"'ground' and 'groundLiquid' need 'support': \"below\""_s);
    }
}

void BlockReader::readGrowth(QcJsonFieldReader &growthReader)
{
    m_block.growth.maxHeight = growthReader.integer(u"maxHeight"_s, 3, 2, 64);
    m_block.growth.averageSeconds = growthReader.number(u"averageSeconds"_s, 600.0, 1.0, 86400.0);
    if (growthReader.ok() && m_block.support != QcBlockSupport::Below) {
        growthReader.fail(u"growing blocks need 'support': \"below\""_s);
    }
}

void BlockReader::readFarming()
{
    m_reader.nestedObject(u"crop"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_block.crop = QcFarmingParser::parseCrop(m_block, nested, m_namespace);
    });
    m_reader.nestedObject(u"soil"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_block.soil = QcFarmingParser::parseSoil(m_block, nested, m_namespace);
    });
    m_block.tillsInto = m_reader.name(u"tillsInto"_s, m_namespace);
    m_block.flattensInto = m_reader.name(u"flattensInto"_s, m_namespace);
    m_block.coveredInto = m_reader.name(u"coveredInto"_s, m_namespace);
    m_reader.nestedObject(u"carving"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_block.carvesInto = nested.name(u"into"_s, m_namespace);
        m_block.carveDrop = nested.name(u"drop"_s, m_namespace);
        m_block.carveDropCount = nested.integer(u"count"_s, m_block.carveDrop.isEmpty() ? 0 : 1, 0, 64);
        if (m_block.carvesInto.isEmpty()) {
            nested.fail(u"'carving' needs the block it turns 'into'"_s);
        }
    });
    if (m_block.crop.isCrop() && m_block.growth.grows()) {
        m_reader.fail(u"a block grows either upwards ('growth') or in stages ('crop')"_s);
    }
}

void BlockReader::readSounds(QcJsonFieldReader &soundReader)
{
    for (int action = 0; action < qcBlockSoundActionCount; ++action) {
        m_block.soundKeys[std::size_t(action)] = soundReader.name(QcSoundKeys::actionName(QcBlockSoundAction(action)), m_namespace);
    }
}

void BlockReader::readTextureSlots(const QJsonObject &faces, QcJsonFieldReader &textureReader)
{
    const QJsonObject slotJson = faces.value(u"slots"_s).toObject();
    if (slotJson.size() > 16 || (faces.contains(u"slots"_s) && !faces.value(u"slots"_s).isObject())) {
        textureReader.fail(u"'slots' must be an object of at most 16 named textures"_s);
    }

    for (auto it = slotJson.constBegin(); it != slotJson.constEnd(); ++it) {
        const QString texture = it.value().isString() ? QcContentNames::qualify(it.value().toString(), m_namespace) : QString();
        if (texture.isEmpty() || it.key().isEmpty() || it.key().size() > 32) {
            textureReader.fail(u"invalid texture slot '%1'"_s.arg(it.key()));
            break;
        }
        m_block.textureSlots.append({it.key(), texture});
    }
}

void BlockReader::readTextures()
{
    if (m_block.renderLayer == QcRenderLayer::Invisible) {
        return;
    }

    const QJsonValue textures = m_json.value(u"textures"_s);
    if (!textures.isObject()) {
        m_reader.fail(u"'textures' object is required for visible blocks"_s);
        return;
    }

    const QJsonObject faces = textures.toObject();
    QcJsonFieldReader textureReader{faces, m_reader.context + u" textures"_s, {}};
    const QString all = textureReader.name(u"all"_s, m_namespace);
    const QString side = textureReader.name(u"side"_s, m_namespace, all);
    const QString top = textureReader.name(u"top"_s, m_namespace, all);
    const QString bottom = textureReader.name(u"bottom"_s, m_namespace, all);
    const auto face = [&](const QString &key, const QString &fallback) {
        return textureReader.name(key, m_namespace, fallback);
    };
    m_block.faceTextures = {face(u"east"_s, side), face(u"west"_s, side), top,
                            bottom, face(u"south"_s, side), face(u"north"_s, side)};
    readTextureSlots(faces, textureReader);
    m_reader.adopt(textureReader);

    for (const QString &texture : m_block.faceTextures) {
        if (texture.isEmpty()) {
            m_reader.fail(u"every face needs a texture (use 'all', 'side', 'top', 'bottom' or a face name)"_s);
            break;
        }
    }
}

// The optional nested objects read between the mining fields and the liquid flow.
void BlockReader::readNamedObjects()
{
    m_reader.nestedObject(u"machine"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { readMachine(nested); });
    readDrops();
    m_block.experience = QcContentFields::parseExperience(u"experience"_s, m_reader);
    m_reader.nestedObject(u"infested"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { readInfested(nested); });
    m_reader.nestedObject(u"spawner"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { readSpawner(nested); });
    m_reader.nestedObject(u"sapling"_s, [this](const QJsonObject &object, QcJsonFieldReader &nested) { readSapling(object, nested); });
}

QcResult<QcBlockDefinition> BlockReader::read()
{
    m_block.name = m_reader.name(u"name"_s, m_namespace);
    if (m_block.name.isEmpty()) {
        return qcError(m_reader.ok() ? u"block: 'name' is required"_s : m_reader.error);
    }

    m_reader.context = u"block '%1'"_s.arg(m_block.name);
    m_block.translationKey = m_reader.string(u"translation"_s, QcContentFields::translationKey(u"block"_s, m_block.name));
    m_block.descriptionKey = m_reader.string(u"description"_s, m_block.translationKey + u".description"_s);

    readShapeField();
    readRenderFields();
    readStates();
    readPlacementFields();
    m_reader.nestedObject(u"connects"_s, [this](const QJsonObject &object, QcJsonFieldReader &nested) { readConnects(object, nested); });
    m_reader.nestedObject(u"container"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { readContainer(nested); });

    readMiningFields();
    readNamedObjects();

    readFlow();
    readBehaviourFields();
    readMovementFields();
    readFireFields();
    m_block.blast = QcBlastParser::parseBlast(m_block, m_reader);
    readGround();
    m_reader.nestedObject(u"growth"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { readGrowth(nested); });
    readFarming();
    m_reader.nestedObject(u"signal"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_block.signal = QcSignalParser::parseSignal(m_block, nested, m_namespace);
    });
    m_block.soundMaterial = m_reader.name(u"sound"_s, m_namespace, QcSoundKeys::defaultMaterialFor(m_block));
    m_reader.nestedObject(u"sounds"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { readSounds(nested); });

    readTextures();
    m_block.itemTexture = m_reader.name(u"itemTexture"_s, m_namespace);
    m_block.hasItem = m_reader.boolean(u"item"_s, true);
    m_block.itemTags = QcContentFields::parseTags(m_json.value(u"tags"_s), m_namespace, m_reader);
    checkPartTextures(m_block, m_reader);

    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return std::move(m_block);
}

} // namespace

namespace QcBlockParser {

[[nodiscard]] QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace)
{
    return BlockReader(json, packNamespace).read();
}

} // namespace QcBlockParser
