#include "common/content/parser/QcBlockParser.h"

#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/content/definitions/QcSoundKeys.h"
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

// "cube" and "cross" always name the base shapes; other names resolve in the pack's namespace.
void parseShapeField(const QJsonObject &json, QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue shapeValue = json.value(u"shape"_s);
    if (shapeValue.isObject()) {
        const QJsonObject inlineShape = shapeValue.toObject();
        QcJsonFieldReader shapeReader{inlineShape, reader.context + u" shape"_s, {}};
        QcShapeParser::parseShapeBody(inlineShape, block.shape, shapeReader);
        reader.adopt(shapeReader);
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
}

void parseRenderFields(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const bool cross = isCross(block);
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
}

void parseStates(const QJsonObject &json, QcBlockDefinition &block, QcJsonFieldReader &reader)
{
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
            const std::optional<QString> text = QcContentFields::stateValue(value);
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
}

// Whether the block has the state property `name` with all of `values`.
bool hasValues(const QcBlockDefinition &block, const QString &name, const QStringList &values)
{
    const auto property = std::ranges::find(block.properties, name, &QcBlockProperty::name);
    return property != block.properties.cend() && std::ranges::all_of(values, [&](const QString &value) { return property->values.contains(value); });
}

void parsePlacementFields(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    block.lightLevel = reader.integer(u"light"_s, 0, 0, QcBlockDefinition::maxLightLevel);
    block.climbable = reader.boolean(u"climbable"_s, false);
    block.toggles = reader.boolean(u"toggles"_s, false);
    block.door = reader.boolean(u"door"_s, false);
    block.bed = reader.boolean(u"bed"_s, false);
    // A bed is placed as two halves along its facing: without both it could never be placed.
    if (block.bed && (!hasValues(block, u"part"_s, {u"foot"_s, u"head"_s})
                      || !hasValues(block, u"facing"_s, {u"north"_s, u"east"_s, u"south"_s, u"west"_s}))) {
        reader.fail(u"a bed needs the states 'part' (foot, head) and 'facing' (north, east, south, west)"_s);
    }

    const QString facing = reader.string(u"facing"_s, u"away"_s);
    if (facing == u"toward"_s) {
        block.facingRule = QcFacingRule::Toward;
    } else if (facing == u"into"_s) {
        block.facingRule = QcFacingRule::Into;
    } else if (facing != u"away"_s) {
        reader.fail(u"'facing' must be away, toward or into"_s);
    }

    const QString support = reader.string(u"support"_s, isCross(block) ? u"below"_s : u"none"_s);
    if (support == u"below"_s) {
        block.support = QcBlockSupport::Below;
    } else if (support == u"attached"_s) {
        block.support = QcBlockSupport::Attached;
    } else if (support != u"none"_s) {
        reader.fail(u"'support' must be none, below or attached"_s);
    }
}

void parseConnects(const QJsonObject &connectJson, QcBlockDefinition &block, QcJsonFieldReader &connectReader)
{
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
}

void parseContainer(const QJsonObject &blockJson, QcBlockDefinition &block, QcJsonFieldReader &containerReader)
{
    block.containerSlots = containerReader.integer(u"slots"_s, 27, 1, QcBlockDefinition::maxContainerSlots);
    block.containerColumns = containerReader.integer(u"columns"_s, 9, 1, 9);
    if (containerReader.ok() && (block.opensCrafting || blockJson.contains(u"machine"_s))) {
        containerReader.fail(u"a block cannot store items and also be a crafting table or machine"_s);
    }
}

void parseMiningFields(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    block.lightOpacity = reader.integer(u"lightOpacity"_s, block.opaque ? 15 : (block.liquid ? 2 : 0), 0, 15);
    block.hardness = reader.number(u"hardness"_s, 1.0, -1.0, 1000.0);
    block.tool = reader.string(u"tool"_s);
    block.toolLevel = reader.integer(u"toolLevel"_s, 0, 0, 100);
    block.opensCrafting = reader.boolean(u"crafting"_s, false);
    block.fuelSeconds = reader.number(u"fuelSeconds"_s, 0.0, 0.0, QcContentFields::maxFuelSeconds);
}

// An infested block (decision E-453): the block it stands in for and the creature inside.
void parseInfested(QcBlockDefinition &block, QcJsonFieldReader &infestedReader, const QString &packNamespace)
{
    block.infestsBlock = infestedReader.name(u"block"_s, packNamespace);
    block.infestsMob = infestedReader.name(u"mob"_s, packNamespace);
    if (infestedReader.ok() && (block.infestsBlock.isEmpty() || block.infestsMob.isEmpty())) {
        infestedReader.fail(u"needs 'block' and 'mob'"_s);
    }
}

void parseMachine(QcBlockDefinition &block, QcJsonFieldReader &machineReader, const QString &packNamespace)
{
    block.machine.category = machineReader.string(u"category"_s, QString::fromLatin1(QcSmeltingRecipe::defaultCategory));
    block.machine.speed = machineReader.number(u"speed"_s, 1.0, minMachineSpeed, maxMachineSpeed);
    block.machine.sound = machineReader.name(u"sound"_s, packNamespace, QString::fromLatin1(QcSoundKeys::machineActive));

    if (machineReader.ok() && !QcContentFields::isValidCategory(block.machine.category)) {
        machineReader.fail(QcContentFields::invalidCategoryMessage());
    }
    if (machineReader.ok() && block.opensCrafting) {
        machineReader.fail(u"a block cannot be a crafting table and a machine"_s);
    }
}

void parseDrops(const QJsonObject &json, QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue drops = json.value(u"drops"_s);
    if (drops.isUndefined()) {
        block.drops = {{block.name, 1}};
        return;
    }

    if (!drops.isArray() || drops.toArray().size() > 64) {
        reader.fail(u"'drops' must be an array of at most 64 entries"_s);
        return;
    }

    const QJsonArray dropList = drops.toArray();
    for (const auto &entry : dropList) {
        if (!entry.isObject()) {
            reader.fail(u"'drops' entries must be objects"_s);
            break;
        }
        const QJsonObject dropJson = entry.toObject();
        QcJsonFieldReader dropReader{dropJson, reader.context, {}};
        const QcItemDrop drop{dropReader.name(u"item"_s, packNamespace), dropReader.integer(u"count"_s, 1, 1, 64),
                              dropReader.number(u"chance"_s, 1.0, 0.0, 1.0), QcShapeParser::parseConditions(dropJson, dropReader),
                              dropReader.integer(u"maxCount"_s, 0, 0, 64)};
        if (!dropReader.ok() || drop.item.isEmpty()) {
            reader.fail(dropReader.ok() ? u"drop needs an 'item'"_s : dropReader.error);
            break;
        }
        // Conditions name states of the block itself (decision E-289).
        for (const QcShapeCondition &condition : drop.when) {
            const QcBlockProperty *property = block.property(condition.property);
            const bool known = property && std::ranges::all_of(condition.values, [property](const QString &value) {
                                   return property->values.contains(value);
                               });
            if (!known) {
                reader.fail(u"drop 'when' names an unknown state '%1'"_s.arg(condition.property));
            }
        }
        block.drops.append(drop);
    }
}

void parseSapling(const QJsonObject &saplingJson, QcBlockDefinition &block, QcJsonFieldReader &saplingReader,
                  const QString &packNamespace)
{
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
}

void parseLiquidReactions(const QJsonObject &flowJson, QcLiquidDefinition &liquid, QcJsonFieldReader &flowReader,
                          const QString &packNamespace)
{
    const QJsonValue reactions = flowJson.value(u"reactions"_s);
    if (!reactions.isUndefined() && (!reactions.isArray() || reactions.toArray().size() > 16)) {
        flowReader.fail(u"'reactions' must be an array of at most 16 entries"_s);
    }

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
        liquid.reactions.append(reaction);
    }
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

void parseFlow(const QJsonObject &json, QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue flow = json.value(u"flow"_s);
    if (!flow.isUndefined() && (!flow.isObject() || !block.liquid)) {
        reader.fail(u"'flow' must be an object and needs 'liquid': true"_s);
        return;
    }
    if (!flow.isObject()) {
        return;
    }

    const QJsonObject flowJson = flow.toObject();
    QcJsonFieldReader flowReader{flowJson, reader.context + u" flow"_s, {}};
    QcLiquidDefinition &liquid = block.flow;
    liquid.distance = flowReader.integer(u"distance"_s, 7, 1, QcLiquidDefinition::maxDistance);
    liquid.ticks = flowReader.integer(u"ticks"_s, 5, 1, QcLiquidDefinition::maxTicks);
    liquid.infinite = flowReader.boolean(u"infinite"_s, false);
    liquid.dropSearch = flowReader.integer(u"dropSearch"_s, 4, 0, QcLiquidDefinition::maxDropSearch);
    liquid.movementFactor = flowReader.number(u"movement"_s, 1.0, 0.05, 2.0);
    liquid.bucket = flowReader.name(u"bucket"_s, packNamespace);
    liquid.bottle = flowReader.name(u"bottle"_s, packNamespace);
    liquid.sound = flowReader.name(u"sound"_s, packNamespace);

    parseLiquidReactions(flowJson, liquid, flowReader, packNamespace);
    checkLiquidStates(block, flowReader);
    reader.adopt(flowReader);
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

void parseRailAction(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const QString action = reader.string(u"railAction"_s, u"none"_s);
    if (action == u"boost"_s) {
        block.railAction = QcRailAction::Boost;
    } else if (action == u"activate"_s) {
        block.railAction = QcRailAction::Activate;
    } else if (action != u"none"_s) {
        reader.fail(u"'railAction' must be none, boost or activate"_s);
    }
    if (block.railAction != QcRailAction::None && !block.rail) {
        reader.fail(u"only a rail has a 'railAction'"_s);
    }
}

void parseBehaviourFields(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    block.contactDamage = reader.number(u"contactDamage"_s, 0.0, 0.0, 100.0);
    block.washable = reader.boolean(u"washable"_s, isCross(block));
    block.falls = reader.boolean(u"falls"_s, false);
    if (block.falls && (block.liquid || !block.solid)) {
        reader.fail(u"only solid blocks can fall"_s);
    }
    block.rail = reader.boolean(u"rail"_s, false);
    parseRailAction(block, reader);
    block.supportsAbove = reader.boolean(u"supportsAbove"_s, false);
    if (block.rail) {
        checkRailStates(block, reader);
    }
}

// Fire, flammability and eternal fire (decision E-392).
void parseFireFields(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    QcFireDefinition &fire = block.fire;
    fire.isFire = reader.boolean(u"fire"_s, false);
    fire.eternal = reader.boolean(u"eternalFire"_s, false);
    fire.setsAlightSeconds = reader.integer(u"setsAlight"_s, 0, 0, QcFireDefinition::maxAlightSeconds);
    fire.sparks = reader.integer(u"sparks"_s, 0, 0, QcFireDefinition::maxChance);
    reader.nestedObject(u"flammable"_s, [&fire](const QJsonObject &, QcJsonFieldReader &nested) {
        fire.ignite = nested.integer(u"ignite"_s, 0, 0, QcFireDefinition::maxChance);
        fire.burn = nested.integer(u"burn"_s, 0, 0, QcFireDefinition::maxChance);
    });
    if (fire.isFire && (block.solid || block.liquid || fire.isFlammable() || fire.eternal)) {
        reader.fail(u"fire must be neither solid nor liquid, flammable or eternal"_s);
    }
    if (fire.isFire && block.lightOpacity != 0) {
        reader.fail(u"fire must have 'lightOpacity' 0"_s);
    }
    if (block.liquid && fire.isFlammable()) {
        reader.fail(u"a liquid cannot be flammable"_s);
    }
    if (fire.setsAlightSeconds > 0 && block.solid) {
        reader.fail(u"'setsAlight' needs a block bodies can stand in (not solid)"_s);
    }
}

// Slowing inside (cobweb), the walking surface (mud, ice) and hardening next to another block
// (concrete powder), decisions E-207 and E-208.
void parseMovementFields(QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
    block.slowsInside = reader.number(u"slowsInside"_s, 1.0, 0.01, 1.0);
    if (block.slowsInside < 1.0 && (block.solid || block.liquid)) {
        reader.fail(u"only blocks that are neither solid nor liquid can slow bodies inside"_s);
    }

    reader.nestedObject(u"surface"_s, [&block](const QJsonObject &, QcJsonFieldReader &nested) {
        block.surfaceSpeed = nested.number(u"speed"_s, 1.0, 0.1, 2.0);
        block.surfaceJump = nested.number(u"jump"_s, 1.0, 0.1, 2.0);
        block.slipperiness = nested.number(u"slipperiness"_s, QcBlockDefinition::grippingSlipperiness, QcBlockDefinition::grippingSlipperiness,
                                           QcBlockDefinition::maxSlipperiness);
    });
    reader.nestedObject(u"hardens"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        block.hardensWith = nested.name(u"with"_s, packNamespace);
        block.hardensInto = nested.name(u"into"_s, packNamespace);
        if (nested.ok() && (block.hardensWith.isEmpty() || block.hardensInto.isEmpty())) {
            nested.fail(u"needs 'with' and 'into'"_s);
        }
        if (nested.ok() && (block.hardensInto == block.name || block.hardensWith == block.name)) {
            nested.fail(u"a block cannot harden into or next to itself"_s);
        }
    });
}

void parseGround(const QJsonObject &json, QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
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
}

void parseGrowth(QcBlockDefinition &block, QcJsonFieldReader &growthReader)
{
    QcGrowthDefinition &growth = block.growth;
    growth.maxHeight = growthReader.integer(u"maxHeight"_s, 3, 2, 64);
    growth.averageSeconds = growthReader.number(u"averageSeconds"_s, 600.0, 1.0, 86400.0);
    if (growthReader.ok() && block.support != QcBlockSupport::Below) {
        growthReader.fail(u"growing blocks need 'support': \"below\""_s);
    }
}

void parseFarming(QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
    reader.nestedObject(u"crop"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        QcFarmingParser::parseCrop(block, nested, packNamespace);
    });
    reader.nestedObject(u"soil"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        QcFarmingParser::parseSoil(block, nested, packNamespace);
    });
    block.tillsInto = reader.name(u"tillsInto"_s, packNamespace);
    block.flattensInto = reader.name(u"flattensInto"_s, packNamespace);
    block.coveredInto = reader.name(u"coveredInto"_s, packNamespace);
    reader.nestedObject(u"carving"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        block.carvesInto = nested.name(u"into"_s, packNamespace);
        block.carveDrop = nested.name(u"drop"_s, packNamespace);
        block.carveDropCount = nested.integer(u"count"_s, block.carveDrop.isEmpty() ? 0 : 1, 0, 64);
        if (block.carvesInto.isEmpty()) {
            nested.fail(u"'carving' needs the block it turns 'into'"_s);
        }
    });
    if (block.crop.isCrop() && block.growth.grows()) {
        reader.fail(u"a block grows either upwards ('growth') or in stages ('crop')"_s);
    }
}

void parseSounds(QcBlockDefinition &block, QcJsonFieldReader &soundReader, const QString &packNamespace)
{
    for (int action = 0; action < qcBlockSoundActionCount; ++action) {
        block.soundKeys[std::size_t(action)] = soundReader.name(QcSoundKeys::actionName(QcBlockSoundAction(action)), packNamespace);
    }
}

void parseTextureSlots(const QJsonObject &faces, QcBlockDefinition &block, QcJsonFieldReader &textureReader,
                       const QString &packNamespace)
{
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
}

void parseTextures(const QJsonObject &json, QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
{
    if (block.renderLayer == QcRenderLayer::Invisible) {
        return;
    }

    const QJsonValue textures = json.value(u"textures"_s);
    if (!textures.isObject()) {
        reader.fail(u"'textures' object is required for visible blocks"_s);
        return;
    }

    const QJsonObject faces = textures.toObject();
    QcJsonFieldReader textureReader{faces, reader.context + u" textures"_s, {}};
    const QString all = textureReader.name(u"all"_s, packNamespace);
    const QString side = textureReader.name(u"side"_s, packNamespace, all);
    const QString top = textureReader.name(u"top"_s, packNamespace, all);
    const QString bottom = textureReader.name(u"bottom"_s, packNamespace, all);
    const auto face = [&](const QString &key, const QString &fallback) {
        return textureReader.name(key, packNamespace, fallback);
    };
    block.faceTextures = {face(u"east"_s, side), face(u"west"_s, side), top,
                          bottom, face(u"south"_s, side), face(u"north"_s, side)};
    parseTextureSlots(faces, block, textureReader, packNamespace);
    reader.adopt(textureReader);

    for (const QString &texture : block.faceTextures) {
        if (texture.isEmpty()) {
            reader.fail(u"every face needs a texture (use 'all', 'side', 'top', 'bottom' or a face name)"_s);
            break;
        }
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

} // namespace

namespace QcBlockParser {

[[nodiscard]] QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"block"_s, {}};
    QcBlockDefinition block;
    block.name = reader.name(u"name"_s, packNamespace);
    if (block.name.isEmpty()) {
        return qcError(reader.ok() ? u"block: 'name' is required"_s : reader.error);
    }

    reader.context = u"block '%1'"_s.arg(block.name);
    block.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"block"_s, block.name));
    block.descriptionKey = reader.string(u"description"_s, block.translationKey + u".description"_s);

    parseShapeField(json, block, reader, packNamespace);
    parseRenderFields(block, reader);
    parseStates(json, block, reader);
    parsePlacementFields(block, reader);
    reader.nestedObject(u"connects"_s, [&block](const QJsonObject &object, QcJsonFieldReader &nested) {
        parseConnects(object, block, nested);
    });
    reader.nestedObject(u"container"_s, [&json, &block](const QJsonObject &, QcJsonFieldReader &nested) {
        parseContainer(json, block, nested);
    });

    parseMiningFields(block, reader);
    reader.nestedObject(u"machine"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseMachine(block, nested, packNamespace);
    });
    parseDrops(json, block, reader, packNamespace);
    block.experience = QcContentFields::parseExperience(u"experience"_s, reader);
    reader.nestedObject(u"infested"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseInfested(block, nested, packNamespace);
    });
    reader.nestedObject(u"sapling"_s, [&block, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        parseSapling(object, block, nested, packNamespace);
    });

    parseFlow(json, block, reader, packNamespace);
    parseBehaviourFields(block, reader);
    parseMovementFields(block, reader, packNamespace);
    parseFireFields(block, reader);
    parseGround(json, block, reader, packNamespace);
    reader.nestedObject(u"growth"_s, [&block](const QJsonObject &, QcJsonFieldReader &nested) { parseGrowth(block, nested); });
    parseFarming(block, reader, packNamespace);
    reader.nestedObject(u"signal"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        QcSignalParser::parseSignal(block, nested, packNamespace);
    });
    block.soundMaterial = reader.name(u"sound"_s, packNamespace, QcSoundKeys::defaultMaterialFor(block));
    reader.nestedObject(u"sounds"_s, [&block, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseSounds(block, nested, packNamespace);
    });

    parseTextures(json, block, reader, packNamespace);
    block.itemTexture = reader.name(u"itemTexture"_s, packNamespace);
    block.hasItem = reader.boolean(u"item"_s, true);
    block.itemTags = QcContentFields::parseTags(json.value(u"tags"_s), packNamespace, reader);
    checkPartTextures(block, reader);

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return block;
}

} // namespace QcBlockParser
