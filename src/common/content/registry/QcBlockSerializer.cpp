#include "QcBlockSerializer.h"

#include "QcBlockStateTable.h"
#include "QcStreamLimits.h"

#include <cmath>

namespace {

void writeAabbs(QDataStream &stream, const QList<QcAabb> &boxes)
{
    stream << quint32(boxes.size());
    for (const QcAabb &box : boxes) {
        stream << box.min.x << box.min.y << box.min.z << box.max.x << box.max.y << box.max.z;
    }
}

bool readAabbs(QDataStream &stream, QList<QcAabb> &boxes, double maxY)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcShapeDefinition::maxBoxesPerPart, count)) {
        return false;
    }

    for (quint32 i = 0; i < count; ++i) {
        QcAabb box;
        stream >> box.min.x >> box.min.y >> box.min.z >> box.max.x >> box.max.y >> box.max.z;
        // Boxes feed collision, raycasts and the mesher: they must lie within their cell.
        const bool inside = box.min.x >= 0.0 && box.max.x <= 1.0 && box.min.z >= 0.0 && box.max.z <= 1.0
                            && box.min.y >= 0.0 && box.max.y <= maxY;
        if (stream.status() != QDataStream::Ok || !inside || !(box.min.x < box.max.x) || !(box.min.y < box.max.y)
            || !(box.min.z < box.max.z)) {
            stream.setStatus(QDataStream::ReadCorruptData);
            return false;
        }
        boxes.append(box);
    }

    return true;
}

void writeShape(QDataStream &stream, const QcShapeDefinition &shape)
{
    stream << shape.name << shape.rotate << shape.flip << quint32(shape.parts.size());
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
        stream << part.texture << part.stretch;
    }
}

bool readShape(QDataStream &stream, QcShapeDefinition &shape)
{
    quint32 partCount = 0;
    stream >> shape.name >> shape.rotate >> shape.flip;
    if (!QcStreamLimits::readCount(stream, QcShapeDefinition::maxParts, partCount)) {
        return false;
    }

    for (quint32 p = 0; p < partCount; ++p) {
        QcShapePart part;
        quint32 conditionCount = 0;
        if (!QcStreamLimits::readCount(stream, QcShapeDefinition::maxConditions, conditionCount)) {
            return false;
        }

        for (quint32 c = 0; c < conditionCount; ++c) {
            QcShapeCondition condition;
            quint32 valueCount = 0;
            stream >> condition.property;
            if (!QcStreamLimits::readCount(stream, QcBlockProperty::maxValues, valueCount)) {
                return false;
            }
            for (quint32 v = 0; v < valueCount; ++v) {
                QString value;
                stream >> value;
                condition.values.append(value);
            }
            part.when.append(std::move(condition));
        }

        if (!readAabbs(stream, part.boxes, 1.0) || !readAabbs(stream, part.crosses, 1.0)) {
            return false;
        }
        stream >> part.customCollision;
        if (!readAabbs(stream, part.collision, QcShapeDefinition::maxCollisionHeight)) {
            return false;
        }
        stream >> part.texture >> part.stretch;
        shape.parts.append(std::move(part));
    }

    return stream.status() == QDataStream::Ok && shape.boxCount() <= QcShapeDefinition::maxBoxes;
}

// Reads the fixed leading fields and checks the ones that drive server-side timers and rendering.
bool readBlockCore(QDataStream &stream, QcBlockDefinition &block)
{
    quint8 renderLayer = 0;
    qint32 lightOpacity = 0;
    qint32 toolLevel = 0;
    stream >> block.name >> block.translationKey >> renderLayer >> block.solid >> block.opaque >> block.liquid
        >> block.replaceable >> lightOpacity >> block.hardness >> block.tool >> toolLevel >> block.opensCrafting
        >> block.machine.category >> block.machine.speed >> block.fuelSeconds >> block.machine.sound >> block.soundMaterial;
    for (QString &key : block.soundKeys) {
        stream >> key;
    }

    // Machine speed and fuel time drive server-side timers; they must stay within the parser's ranges.
    const bool machineOk = block.machine.category.size() <= 64 && std::isfinite(block.machine.speed)
                           && block.machine.speed > 0.0 && block.machine.speed <= 100.0;
    if (renderLayer > quint8(QcRenderLayer::Translucent) || lightOpacity < 0 || lightOpacity > 15
        || !machineOk || !QcBlockSerializer::plausibleFuel(block.fuelSeconds)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }

    block.renderLayer = QcRenderLayer(renderLayer);
    block.lightOpacity = lightOpacity;
    block.toolLevel = toolLevel;
    return true;
}

bool readDrops(QDataStream &stream, QList<QcItemDrop> &drops)
{
    quint32 dropCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, dropCount)) {
        return false;
    }

    drops.clear();
    for (quint32 i = 0; i < dropCount; ++i) {
        QcItemDrop drop;
        qint32 count = 0;
        stream >> drop.item >> count >> drop.chance;
        drop.count = count;
        drops.append(drop);
    }

    return true;
}

bool readSapling(QDataStream &stream, QcSaplingDefinition &sapling)
{
    quint32 groundCount = 0;
    stream >> sapling.feature >> sapling.minSeconds >> sapling.maxSeconds >> sapling.plantChance;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, groundCount)) {
        return false;
    }

    sapling.plantOn.clear();
    for (quint32 i = 0; i < groundCount; ++i) {
        QString ground;
        stream >> ground;
        sapling.plantOn.append(ground);
    }

    if (!std::isfinite(sapling.minSeconds) || !std::isfinite(sapling.maxSeconds) || !std::isfinite(sapling.plantChance)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    return true;
}

bool readProperties(QDataStream &stream, QList<QcBlockProperty> &properties)
{
    quint32 propertyCount = 0;
    if (!QcStreamLimits::readCount(stream, QcBlockProperty::maxProperties, propertyCount)) {
        return false;
    }

    for (quint32 i = 0; i < propertyCount; ++i) {
        QcBlockProperty property;
        quint32 valueCount = 0;
        stream >> property.name;
        if (!QcStreamLimits::readCount(stream, QcBlockProperty::maxValues, valueCount)) {
            return false;
        }

        for (quint32 v = 0; v < valueCount; ++v) {
            QString value;
            stream >> value;
            property.values.append(value);
        }
        properties.append(std::move(property));
    }

    return true;
}

bool readTextureSlots(QDataStream &stream, QList<std::pair<QString, QString>> &textureSlots)
{
    quint32 slotCount = 0;
    if (!QcStreamLimits::readCount(stream, 16, slotCount)) {
        return false;
    }

    for (quint32 i = 0; i < slotCount; ++i) {
        QString slot;
        QString texture;
        stream >> slot >> texture;
        textureSlots.append({slot, texture});
    }

    return true;
}

// Reads the trailing fields (placement, connections, storage, flow) and checks the whole block.
bool readBlockBehaviour(QDataStream &stream, QcBlockDefinition &block)
{
    qint32 lightLevel = 0;
    quint8 facingRule = 0;
    quint8 support = 0;
    quint32 groupCount = 0;
    stream >> block.itemTexture >> lightLevel >> block.climbable >> block.toggles >> block.door >> facingRule >> support
        >> block.connectGroup;
    if (!QcStreamLimits::readCount(stream, 16, groupCount)) {
        return false;
    }

    for (quint32 i = 0; i < groupCount; ++i) {
        QString group;
        stream >> group;
        block.connectsTo.append(group);
    }

    qint32 containerSlots = 0;
    qint32 flowDistance = 0;
    stream >> containerSlots >> flowDistance >> block.flow.movementFactor;
    stream >> block.slowsInside >> block.surfaceSpeed >> block.surfaceJump;
    const auto factor = [](double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; };
    if (flowDistance < 0 || flowDistance > QcLiquidDefinition::maxDistance || !factor(block.flow.movementFactor, 0.05, 2.0)
        || !factor(block.slowsInside, 0.01, 1.0) || !factor(block.surfaceSpeed, 0.1, 2.0) || !factor(block.surfaceJump, 0.1, 2.0)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }

    block.flow.distance = flowDistance;
    if (stream.status() != QDataStream::Ok || lightLevel < 0 || lightLevel > QcBlockDefinition::maxLightLevel
        || facingRule > quint8(QcFacingRule::Toward) || support > quint8(QcBlockSupport::Attached) || containerSlots < 0
        || containerSlots > QcBlockDefinition::maxContainerSlots || !QcBlockStateTable::problem(block).isEmpty()) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }

    block.lightLevel = lightLevel;
    block.facingRule = QcFacingRule(facingRule);
    block.support = QcBlockSupport(support);
    block.containerSlots = containerSlots;
    return true;
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
           << qint32(block.toolLevel) << block.opensCrafting << block.machine.category
           << block.machine.speed << block.fuelSeconds << block.machine.sound << block.soundMaterial;
    for (const QString &key : block.soundKeys) {
        stream << key;
    }

    stream << quint32(block.drops.size());
    for (const QcItemDrop &drop : block.drops) {
        stream << drop.item << qint32(drop.count) << drop.chance;
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
    stream << qint32(block.containerSlots) << qint32(block.flow.distance) << block.flow.movementFactor;
    stream << block.slowsInside << block.surfaceSpeed << block.surfaceJump;
}

bool readBlock(QDataStream &stream, QcBlockDefinition &block)
{
    if (!readBlockCore(stream, block) || !readDrops(stream, block.drops) || !readSapling(stream, block.sapling)) {
        return false;
    }

    for (QString &texture : block.faceTextures) {
        stream >> texture;
    }

    if (!readProperties(stream, block.properties) || !readShape(stream, block.shape)
        || !readTextureSlots(stream, block.textureSlots) || !readBlockBehaviour(stream, block)) {
        return false;
    }
    return stream.status() == QDataStream::Ok;
}

} // namespace QcBlockSerializer
