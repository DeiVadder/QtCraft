#include "common/content/registry/QcBlockSerializer.h"

#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/registry/QcBlockStateTable.h"
#include "common/content/registry/QcHoldSerializer.h"
#include "common/protocol/QcStreamLimits.h"

#include <cmath>

namespace {

void writeAabbs(QDataStream &stream, const QList<QcAabb> &boxes)
{
    stream << quint32(boxes.size());
    for (const QcAabb &box : boxes) {
        stream << box.min.x << box.min.y << box.min.z << box.max.x << box.max.y << box.max.z;
    }
}

[[nodiscard]] std::optional<QList<QcAabb>> readAabbs(QDataStream &stream, double maxY)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcShapeDefinition::maxBoxesPerPart);
    if (!countRead) {
        return std::nullopt;
    }
    const quint32 count = *countRead;

    QList<QcAabb> boxes;
    boxes.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        QcAabb box;
        stream >> box.min.x >> box.min.y >> box.min.z >> box.max.x >> box.max.y >> box.max.z;
        // Boxes feed collision, raycasts and the mesher: they must lie within their cell.
        const bool inside = box.min.x >= 0.0 && box.max.x <= 1.0 && box.min.z >= 0.0 && box.max.z <= 1.0
                            && box.min.y >= 0.0 && box.max.y <= maxY;
        if (stream.status() != QDataStream::Ok || !inside || !(box.min.x < box.max.x) || !(box.min.y < box.max.y)
            || !(box.min.z < box.max.z)) {
            stream.setStatus(QDataStream::ReadCorruptData);
            return std::nullopt;
        }
        boxes.append(box);
    }

    return boxes;
}

void writeShape(QDataStream &stream, const QcShapeDefinition &shape)
{
    stream << shape.name << shape.rotate << shape.flip << shape.spin << quint32(shape.parts.size());
    for (const QcShapePart &part : shape.parts) {
        stream << quint32(part.when.size());
        for (const QcShapeCondition &condition : part.when) {
            stream << condition.property << quint32(condition.values.size());
            for (const QString &value : condition.values) {
                stream << value;
            }
        }

        writeAabbs(stream, part.boxes);
        writeAabbs(stream, part.crosses);
        stream << part.customCollision;
        writeAabbs(stream, part.collision);
        writeAabbs(stream, part.target);
        stream << part.texture << part.stretch;
    }
}

[[nodiscard]] std::optional<QcShapeCondition> readCondition(QDataStream &stream)
{
    QcShapeCondition condition;
    stream >> condition.property;
    const std::optional<quint32> valueCountRead = QcStreamLimits::readCount(stream, QcBlockProperty::maxValues);
    if (!valueCountRead) {
        return std::nullopt;
    }

    for (quint32 v = 0; v < *valueCountRead; ++v) {
        QString value;
        stream >> value;
        condition.values.append(value);
    }
    return condition;
}

[[nodiscard]] std::optional<QcShapePart> readShapePart(QDataStream &stream)
{
    QcShapePart part;
    const std::optional<quint32> conditionCountRead = QcStreamLimits::readCount(stream, QcShapeDefinition::maxConditions);
    if (!conditionCountRead) {
        return std::nullopt;
    }
    for (quint32 c = 0; c < *conditionCountRead; ++c) {
        std::optional<QcShapeCondition> condition = readCondition(stream);
        if (!condition) {
            return std::nullopt;
        }
        part.when.append(std::move(*condition));
    }

    std::optional<QList<QcAabb>> boxes = readAabbs(stream, 1.0);
    std::optional<QList<QcAabb>> crosses = boxes ? readAabbs(stream, 1.0) : std::nullopt;
    if (!crosses) {
        return std::nullopt;
    }
    stream >> part.customCollision;
    std::optional<QList<QcAabb>> collision = readAabbs(stream, QcShapeDefinition::maxCollisionHeight);
    std::optional<QList<QcAabb>> target = collision ? readAabbs(stream, 1.0) : std::nullopt;
    if (!target) {
        return std::nullopt;
    }

    part.boxes = std::move(*boxes);
    part.crosses = std::move(*crosses);
    part.collision = std::move(*collision);
    part.target = std::move(*target);
    stream >> part.texture >> part.stretch;
    return part;
}

[[nodiscard]] std::optional<QcShapeDefinition> readShape(QDataStream &stream)
{
    QcShapeDefinition shape;
    stream >> shape.name >> shape.rotate >> shape.flip >> shape.spin;
    const std::optional<quint32> partCountRead = QcStreamLimits::readCount(stream, QcShapeDefinition::maxParts);
    if (!partCountRead) {
        return std::nullopt;
    }

    for (quint32 p = 0; p < *partCountRead; ++p) {
        std::optional<QcShapePart> part = readShapePart(stream);
        if (!part) {
            return std::nullopt;
        }
        shape.parts.append(std::move(*part));
    }

    if (stream.status() != QDataStream::Ok || shape.boxCount() > QcShapeDefinition::maxBoxes) {
        return std::nullopt;
    }
    return shape;
}

// Reads the fixed leading fields and checks the ones that drive server-side timers and rendering.
[[nodiscard]] std::optional<QcBlockDefinition> readBlockCore(QDataStream &stream)
{
    QcBlockDefinition block;
    quint8 renderLayer = 0;
    qint32 lightOpacity = 0;
    qint32 toolLevel = 0;
    stream >> block.name >> block.translationKey >> renderLayer >> block.solid >> block.opaque >> block.liquid
        >> block.replaceable >> lightOpacity >> block.hardness >> block.tool >> toolLevel >> block.opensCrafting >> block.opensStonecutter >> block.opensEnchanting
        >> block.machine.category >> block.machine.speed >> block.fuelSeconds >> block.machine.sound >> block.machine.kinds
        >> block.soundMaterial;
    for (QString &key : block.soundKeys) {
        stream >> key;
    }

    // Machine speed and fuel time drive server-side timers; they must stay within the parser's ranges.
    const bool machineOk = block.machine.category.size() <= 64 && std::isfinite(block.machine.speed)
                           && block.machine.speed > 0.0 && block.machine.speed <= 100.0 && block.machine.kinds.size() <= 16;
    if (renderLayer > quint8(QcRenderLayer::Translucent) || lightOpacity < 0 || lightOpacity > 15
        || !machineOk || !QcBlockSerializer::plausibleFuel(block.fuelSeconds)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }

    block.renderLayer = QcRenderLayer(renderLayer);
    block.lightOpacity = lightOpacity;
    block.toolLevel = toolLevel;
    return block;
}

[[nodiscard]] std::optional<QList<QcItemDrop>> readDrops(QDataStream &stream)
{
    const std::optional<quint32> dropCountRead = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!dropCountRead) {
        return std::nullopt;
    }
    const quint32 dropCount = *dropCountRead;

    QList<QcItemDrop> drops;
    for (quint32 i = 0; i < dropCount; ++i) {
        QcItemDrop drop;
        qint32 count = 0;
        qint32 maxCount = 0;
        stream >> drop.item >> count >> drop.chance >> maxCount;
        drop.count = count;
        drop.maxCount = maxCount;
        drops.append(drop);
    }

    return drops;
}

[[nodiscard]] std::optional<QcSaplingDefinition> readSapling(QDataStream &stream)
{
    QcSaplingDefinition sapling;
    stream >> sapling.feature >> sapling.minSeconds >> sapling.maxSeconds >> sapling.plantChance;
    const std::optional<quint32> groundCountRead = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!groundCountRead) {
        return std::nullopt;
    }
    const quint32 groundCount = *groundCountRead;

    for (quint32 i = 0; i < groundCount; ++i) {
        QString ground;
        stream >> ground;
        sapling.plantOn.append(ground);
    }

    if (!std::isfinite(sapling.minSeconds) || !std::isfinite(sapling.maxSeconds) || !std::isfinite(sapling.plantChance)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }
    return sapling;
}

[[nodiscard]] std::optional<QList<QcBlockProperty>> readProperties(QDataStream &stream)
{
    const std::optional<quint32> propertyCountRead = QcStreamLimits::readCount(stream, QcBlockProperty::maxProperties);
    if (!propertyCountRead) {
        return std::nullopt;
    }
    const quint32 propertyCount = *propertyCountRead;

    QList<QcBlockProperty> properties;
    for (quint32 i = 0; i < propertyCount; ++i) {
        QcBlockProperty property;
        stream >> property.name;
        const std::optional<quint32> valueCountRead = QcStreamLimits::readCount(stream, QcBlockProperty::maxValues);
        if (!valueCountRead) {
            return std::nullopt;
        }
        const quint32 valueCount = *valueCountRead;

        for (quint32 v = 0; v < valueCount; ++v) {
            QString value;
            stream >> value;
            property.values.append(value);
        }
        properties.append(std::move(property));
    }

    return properties;
}

[[nodiscard]] std::optional<QList<std::pair<QString, QString>>> readTextureSlots(QDataStream &stream)
{
    const std::optional<quint32> slotCountRead = QcStreamLimits::readCount(stream, 16);
    if (!slotCountRead) {
        return std::nullopt;
    }
    const quint32 slotCount = *slotCountRead;

    QList<std::pair<QString, QString>> textureSlots;
    for (quint32 i = 0; i < slotCount; ++i) {
        QString slot;
        QString texture;
        stream >> slot >> texture;
        textureSlots.append({slot, texture});
    }

    return textureSlots;
}

// The block with its trailing fields (placement, connections, storage, flow) read and the whole block
// checked; std::nullopt on bad data.
[[nodiscard]] std::optional<QcBlockDefinition> withBehaviour(QDataStream &stream, QcBlockDefinition block)
{
    qint32 lightLevel = 0;
    quint8 facingRule = 0;
    quint8 support = 0;
    stream >> block.itemTexture >> lightLevel >> block.climbable >> block.toggles >> block.door >> facingRule >> support
        >> block.connectGroup;
    const std::optional<quint32> groupCountRead = QcStreamLimits::readCount(stream, 16);
    if (!groupCountRead) {
        return std::nullopt;
    }
    const quint32 groupCount = *groupCountRead;

    for (quint32 i = 0; i < groupCount; ++i) {
        QString group;
        stream >> group;
        block.connectsTo.append(group);
    }

    qint32 containerSlots = 0;
    qint32 containerColumns = 0;
    qint32 flowDistance = 0;
    stream >> containerSlots >> containerColumns >> flowDistance >> block.flow.movementFactor;
    quint8 signalKind = 0;
    stream >> block.slowsInside >> block.surfaceSpeed >> block.surfaceJump >> block.slipperiness >> signalKind >> block.fire.isFire;
    quint8 signLines = 0;
    quint8 signLength = 0;
    qint32 biteFood = 0;
    qint32 campfireSlots = 0;
    stream >> signLines >> signLength;
    stream >> block.floats >> biteFood >> campfireSlots;
    block.sign = {signLines, signLength};
    block.bites.food = biteFood;
    block.campfire.slotCount = campfireSlots;
    if (signalKind >= quint8(qcSignalKindCount) || signLines > QcSignDefinition::maxLines || signLength < 1
        || signLength > QcSignDefinition::maxLineLength) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }
    block.signal.kind = QcSignalKind(signalKind);
    const auto factor = [](double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; };
    if (flowDistance < 0 || flowDistance > QcLiquidDefinition::maxDistance || !factor(block.flow.movementFactor, 0.05, 2.0)
        || !factor(block.slowsInside, 0.01, 1.0) || !factor(block.surfaceSpeed, 0.1, 2.0) || !factor(block.surfaceJump, 0.1, 2.0)
        || !factor(block.slipperiness, QcBlockDefinition::grippingSlipperiness, QcBlockDefinition::maxSlipperiness)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }

    block.flow.distance = flowDistance;
    if (stream.status() != QDataStream::Ok || lightLevel < 0 || lightLevel > QcBlockDefinition::maxLightLevel
        || facingRule > quint8(QcFacingRule::Against) || support > quint8(QcBlockSupport::Attached) || containerSlots < 0
        || containerColumns < 1 || containerColumns > 9
        || containerSlots > QcBlockDefinition::maxContainerSlots || biteFood < 0 || biteFood > QcItemDefinition::maxFood
        || campfireSlots < 0 || campfireSlots > QcCampfireDefinition::maxSlots || !QcBlockStateTable::problem(block).isEmpty()) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }

    block.lightLevel = lightLevel;
    block.facingRule = QcFacingRule(facingRule);
    block.support = QcBlockSupport(support);
    block.containerSlots = containerSlots;
    block.containerColumns = containerColumns;
    return block;
}

} // namespace

namespace QcBlockSerializer {

bool plausibleFuel(double seconds)
{
    return std::isfinite(seconds) && seconds >= 0.0 && seconds <= 100000.0;
}

void writeBlock(QDataStream &stream, const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    stream << block.name << block.translationKey << quint8(block.renderLayer) << block.solid << block.opaque
           << block.liquid << block.replaceable << qint32(block.lightOpacity) << block.hardness << block.tool
           << qint32(block.toolLevel) << block.opensCrafting << block.opensStonecutter << block.opensEnchanting << block.machine.category
           << block.machine.speed << block.fuelSeconds << block.machine.sound << block.machine.kinds << block.soundMaterial;
    for (const QString &key : block.soundKeys) {
        stream << key;
    }

    stream << quint32(block.drops.size());
    for (const QcItemDrop &drop : block.drops) {
        stream << drop.item << qint32(drop.count) << drop.chance << qint32(drop.maxCount);
    }

    const QcSaplingDefinition &sapling = block.sapling;
    stream << sapling.feature << sapling.minSeconds << sapling.maxSeconds << sapling.plantChance
           << quint32(sapling.plantOn.size());
    for (const QString &ground : sapling.plantOn) {
        stream << ground;
    }

    for (const QString &texture : block.faceTextures) {
        stream << texture;
    }

    stream << quint32(block.properties.size());
    for (const QcBlockProperty &property : block.properties) {
        stream << property.name << quint32(property.values.size());
        for (const QString &value : property.values) {
            stream << value;
        }
    }

    writeShape(stream, shape);
    stream << quint32(block.textureSlots.size());
    for (const auto &[slot, texture] : block.textureSlots) {
        stream << slot << texture;
    }

    stream << block.itemTexture << qint32(block.lightLevel) << block.climbable << block.toggles << block.door
           << quint8(block.facingRule) << quint8(block.support) << block.connectGroup << quint32(block.connectsTo.size());
    for (const QString &group : block.connectsTo) {
        stream << group;
    }
    stream << qint32(block.containerSlots) << qint32(block.containerColumns) << qint32(block.flow.distance) << block.flow.movementFactor;
    stream << block.slowsInside << block.surfaceSpeed << block.surfaceJump << block.slipperiness << quint8(block.signal.kind);
    // Whether it is fire: the client shows the default fire around burning bodies.
    stream << block.fire.isFire;
    // Signs: the client draws their text and offers the editor (decision E-766).
    stream << quint8(block.sign.lines) << quint8(block.sign.lineLength);
    // What makes the use button act on the block: a lily pad aims at liquids, cake and a lit campfire
    // take the use instead of food being eaten.
    stream << block.floats << qint32(block.bites.food) << qint32(block.campfire.slotCount);
    // How a hand holds its item (decision E-831).
    QcHoldSerializer::write(stream, block.hold);
}

std::optional<QcBlockDefinition> readBlock(QDataStream &stream)
{
    std::optional<QcBlockDefinition> block = readBlockCore(stream);
    std::optional<QList<QcItemDrop>> drops = block ? readDrops(stream) : std::nullopt;
    std::optional<QcSaplingDefinition> sapling = drops ? readSapling(stream) : std::nullopt;
    if (!sapling) {
        return std::nullopt;
    }
    block->drops = std::move(*drops);
    block->sapling = std::move(*sapling);

    for (QString &texture : block->faceTextures) {
        stream >> texture;
    }

    std::optional<QList<QcBlockProperty>> properties = readProperties(stream);
    std::optional<QcShapeDefinition> shape = properties ? readShape(stream) : std::nullopt;
    std::optional<QList<std::pair<QString, QString>>> textureSlots = shape ? readTextureSlots(stream) : std::nullopt;
    if (!textureSlots) {
        return std::nullopt;
    }
    block->properties = std::move(*properties);
    block->shape = std::move(*shape);
    block->textureSlots = std::move(*textureSlots);

    block = withBehaviour(stream, std::move(*block));
    std::optional<QcHoldDisplay> hold = block ? QcHoldSerializer::read(stream) : std::nullopt;
    if (!hold || stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    block->hold = *hold;
    return block;
}

} // namespace QcBlockSerializer
