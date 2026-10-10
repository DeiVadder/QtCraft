#include "QcProtocolCodec.h"

#include "QcStreamLimits.h"

#include <cmath>

namespace QcProtocolCodec {

QDataStream &operator<<(QDataStream &stream, QcVec3 value)
{
    return stream << value.x << value.y << value.z;
}

QDataStream &operator>>(QDataStream &stream, QcVec3 &value)
{
    return stream >> value.x >> value.y >> value.z;
}

QDataStream &operator<<(QDataStream &stream, const QcAppearance &value)
{
    return stream << value.outfit << value.hair << value.skinColor << value.hairColor << value.shirtColor << value.pantsColor;
}

bool readAppearance(QDataStream &stream, QcAppearance &value)
{
    stream >> value.outfit >> value.hair >> value.skinColor >> value.hairColor >> value.shirtColor >> value.pantsColor;
    for (quint32 *color : {&value.skinColor, &value.hairColor, &value.shirtColor, &value.pantsColor}) {
        *color &= 0xFFFFFFu;
    }
    return value.outfit.size() <= QcAppearance::maxNameLength && value.hair.size() <= QcAppearance::maxNameLength;
}

void writePos(QDataStream &stream, QcBlockPos pos)
{
    stream << qint32(pos.x) << qint32(pos.y) << qint32(pos.z);
}

QcBlockPos readPos(QDataStream &stream)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    return {x, y, z};
}

void writeStack(QDataStream &stream, QcItemStack stack)
{
    stream << stack.item << qint32(stack.count) << qint32(stack.damage);
}

QcItemStack readStack(QDataStream &stream)
{
    QcItemStack slot;
    qint32 amount = 0;
    qint32 damage = 0;
    stream >> slot.item >> amount >> damage;
    slot.count = amount;
    slot.damage = damage;
    return slot;
}

void writeStacks(QDataStream &stream, const QList<QcItemStack> &stacks)
{
    stream << quint32(stacks.size());
    for (const QcItemStack slot : stacks) {
        writeStack(stream, slot);
    }
}

bool readStacks(QDataStream &stream, QList<QcItemStack> &stacks)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, 1024, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        stacks.append(readStack(stream));
    }
    return true;
}

void writeEquipment(QDataStream &stream, const QcEquipment &equipment)
{
    for (const QcItemStack &stack : equipment.stacks) {
        writeStack(stream, stack);
    }
}

QcEquipment readEquipment(QDataStream &stream)
{
    QcEquipment equipment;
    for (QcItemStack &stack : equipment.stacks) {
        stack = readStack(stream);
    }
    return equipment;
}

bool isFinite(const QcVec3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace QcProtocolCodec
