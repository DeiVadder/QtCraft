#include "common/content/registry/QcArmourSerializer.h"

#include "common/protocol/QcStreamLimits.h"

#include <cmath>

namespace {

void writeVector(QDataStream &stream, const QcVec3 &v)
{
    stream << v.x << v.y << v.z;
}

bool readVector(QDataStream &stream, QcVec3 &v)
{
    constexpr double limit = 8.0;
    stream >> v.x >> v.y >> v.z;
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= limit && std::abs(v.y) <= limit
           && std::abs(v.z) <= limit;
}

// The count comes first and is checked before anything is reserved: QDataStream's own QStringList
// operator reserves whatever count the stream announces.
bool readCovers(QDataStream &stream, QStringList &covers)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcArmourDefinition::maxCovers, count)) {
        return false;
    }

    covers.clear();
    covers.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        QString slot;
        stream >> slot;
        covers.append(slot);
    }
    return stream.status() == QDataStream::Ok;
}

bool readLayer(QDataStream &stream, QcArmourLayer &layer)
{
    if (!readCovers(stream, layer.covers)) {
        return false;
    }

    stream >> layer.texture >> layer.front >> layer.inflate >> layer.fromY >> layer.toY;
    return stream.status() == QDataStream::Ok && std::isfinite(layer.inflate) && layer.inflate >= 0.0
           && layer.inflate <= QcArmourDefinition::maxInflate && std::isfinite(layer.fromY) && std::isfinite(layer.toY)
           && layer.fromY < layer.toY;
}

bool readPart(QDataStream &stream, QcArmourPart &part)
{
    quint8 bone = 0;
    stream >> bone >> part.texture >> part.front;
    part.bone = QcBoneRole(bone);
    return stream.status() == QDataStream::Ok && bone <= quint8(QcBoneRole::LeftLeg) && readVector(stream, part.from)
           && readVector(stream, part.to);
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

bool read(QDataStream &stream, std::optional<QcArmourDefinition> &armour)
{
    bool present = false;
    stream >> present;
    if (!present) {
        return stream.status() == QDataStream::Ok;
    }

    QcArmourDefinition definition;
    quint8 slot = 0;
    qint32 points = 0;
    quint32 layerCount = 0;
    stream >> slot >> points >> definition.set;
    if (slot > quint8(QcEquipmentSlot::Feet) || points < 0 || points > QcArmourDefinition::maxPoints
        || !QcStreamLimits::readCount(stream, QcArmourDefinition::maxLayers, layerCount)) {
        return false;
    }
    definition.slot = QcEquipmentSlot(slot);
    definition.points = points;

    definition.layers.resize(layerCount);
    for (QcArmourLayer &layer : definition.layers) {
        if (!readLayer(stream, layer)) {
            return false;
        }
    }

    quint32 partCount = 0;
    if (!QcStreamLimits::readCount(stream, QcArmourDefinition::maxParts, partCount)) {
        return false;
    }
    definition.parts.resize(partCount);
    for (QcArmourPart &part : definition.parts) {
        if (!readPart(stream, part)) {
            return false;
        }
    }

    armour = std::move(definition);
    return stream.status() == QDataStream::Ok;
}

} // namespace QcArmourSerializer
