#include "common/content/registry/QcArmourSerializer.h"

#include "common/protocol/QcStreamLimits.h"

#include <cmath>

namespace {

void writeVector(QDataStream &stream, const QcVec3 &v)
{
    stream << v.x << v.y << v.z;
}

[[nodiscard]] std::optional<QcVec3> readVector(QDataStream &stream)
{
    constexpr double limit = 8.0;
    QcVec3 v;
    stream >> v.x >> v.y >> v.z;
    const bool valid = std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= limit
                       && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
    return valid ? std::optional(v) : std::nullopt;
}

// The count comes first and is checked before anything is reserved: QDataStream's own QStringList
// operator reserves whatever count the stream announces.
[[nodiscard]] std::optional<QStringList> readCovers(QDataStream &stream)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcArmourDefinition::maxCovers);
    if (!countRead) {
        return std::nullopt;
    }
    const quint32 count = *countRead;

    QStringList covers;
    covers.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        QString slot;
        stream >> slot;
        covers.append(slot);
    }
    return stream.status() == QDataStream::Ok ? std::optional(covers) : std::nullopt;
}

[[nodiscard]] std::optional<QcArmourLayer> readLayer(QDataStream &stream)
{
    std::optional<QStringList> covers = readCovers(stream);
    if (!covers) {
        return std::nullopt;
    }

    QcArmourLayer layer;
    layer.covers = std::move(*covers);
    stream >> layer.texture >> layer.front >> layer.inflate >> layer.fromY >> layer.toY;
    const bool valid = stream.status() == QDataStream::Ok && std::isfinite(layer.inflate) && layer.inflate >= 0.0
                       && layer.inflate <= QcArmourDefinition::maxInflate && std::isfinite(layer.fromY)
                       && std::isfinite(layer.toY) && layer.fromY < layer.toY;
    return valid ? std::optional(std::move(layer)) : std::nullopt;
}

[[nodiscard]] std::optional<QcArmourPart> readPart(QDataStream &stream)
{
    QcArmourPart part;
    quint8 bone = 0;
    stream >> bone >> part.texture >> part.front;
    part.bone = QcBoneRole(bone);
    if (stream.status() != QDataStream::Ok || bone > quint8(QcBoneRole::LeftLeg)) {
        return std::nullopt;
    }

    const std::optional<QcVec3> from = readVector(stream);
    const std::optional<QcVec3> to = from ? readVector(stream) : std::nullopt;
    if (!to) {
        return std::nullopt;
    }
    part.from = *from;
    part.to = *to;
    return part;
}

} // namespace

namespace QcArmourSerializer {

void write(QDataStream &stream, const std::optional<QcArmourDefinition> &armour)
{
    stream << armour.has_value();
    if (!armour) {
        return;
    }

    stream << quint8(armour->slot) << qint32(armour->points) << armour->set << quint32(armour->layers.size());
    for (const QcArmourLayer &layer : armour->layers) {
        stream << quint32(layer.covers.size());
        for (const QString &slot : layer.covers) {
            stream << slot;
        }
        stream << layer.texture << layer.front << layer.inflate << layer.fromY << layer.toY;
    }
    stream << quint32(armour->parts.size());
    for (const QcArmourPart &part : armour->parts) {
        stream << quint8(part.bone) << part.texture << part.front;
        writeVector(stream, part.from);
        writeVector(stream, part.to);
    }
}

std::optional<std::optional<QcArmourDefinition>> read(QDataStream &stream)
{
    bool present = false;
    stream >> present;
    if (!present) {
        return stream.status() == QDataStream::Ok ? std::optional<std::optional<QcArmourDefinition>>(std::in_place) : std::nullopt;
    }

    QcArmourDefinition definition;
    quint8 slot = 0;
    qint32 points = 0;
    stream >> slot >> points >> definition.set;
    if (slot > quint8(QcEquipmentSlot::Feet) || points < 0 || points > QcArmourDefinition::maxPoints) {
        return std::nullopt;
    }
    const std::optional<quint32> layerCountRead = QcStreamLimits::readCount(stream, QcArmourDefinition::maxLayers);
    if (!layerCountRead) {
        return std::nullopt;
    }
    definition.slot = QcEquipmentSlot(slot);
    definition.points = points;

    definition.layers.reserve(*layerCountRead);
    for (quint32 i = 0; i < *layerCountRead; ++i) {
        std::optional<QcArmourLayer> layer = readLayer(stream);
        if (!layer) {
            return std::nullopt;
        }
        definition.layers.append(std::move(*layer));
    }

    const std::optional<quint32> partCountRead = QcStreamLimits::readCount(stream, QcArmourDefinition::maxParts);
    if (!partCountRead) {
        return std::nullopt;
    }
    definition.parts.reserve(*partCountRead);
    for (quint32 i = 0; i < *partCountRead; ++i) {
        const std::optional<QcArmourPart> part = readPart(stream);
        if (!part) {
            return std::nullopt;
        }
        definition.parts.append(*part);
    }

    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    return std::optional(std::move(definition));
}

} // namespace QcArmourSerializer
