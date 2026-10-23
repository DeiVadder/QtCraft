#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcStreamLimits.h"

#include <array>
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
    return stream << value.outfit << value.hair << value.skinColor << value.hairColor << value.shirtColor << value.pantsColor << value.cape;
}

std::optional<QcAppearance> readAppearance(QDataStream &stream)
{
    QcAppearance value;

    stream >> value.outfit >> value.hair >> value.skinColor >> value.hairColor >> value.shirtColor >> value.pantsColor >> value.cape;
    for (quint32 *color : {&value.skinColor, &value.hairColor, &value.shirtColor, &value.pantsColor}) {
        *color &= 0xFFFFFFu;
    }
    const bool valid = value.outfit.size() <= QcAppearance::maxNameLength && value.hair.size() <= QcAppearance::maxNameLength
                       && value.cape.size() <= QcAppearance::maxNameLength;
    return valid ? std::optional(std::move(value)) : std::nullopt;
}

void writePos(QDataStream &stream, QcBlockPos pos)
{
    stream << static_cast<qint32>(pos.x) << static_cast<qint32>(pos.y) << static_cast<qint32>(pos.z);
}

QcBlockPos readPos(QDataStream &stream)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    return {x, y, z};
}

namespace {

// The components of item data on the wire (decision E-823), one bit each in a byte after the
// damage, which only stacks with data have (stackDataFlag set in the count): a plain stack is as
// long as before. The components a save keeps without knowing them never go on the wire.
constexpr qint32 stackDataFlag = 0x4000'0000;

enum StackBits : quint8 {
    MapIdBit = 1,
    ContentsBit = 2,
    NameBit = 4,
    EnchantmentsBit = 8,
    KnownBits = MapIdBit | ContentsBit | NameBit | EnchantmentsBit,
};

// The component bits of `data` on the wire; 0 for none.
quint8 bitsOf(const QcItemData &data)
{
    return static_cast<quint8>((data.mapId != 0 ? MapIdBit : 0) | (data.contents.isEmpty() ? 0 : ContentsBit) | (data.name.isEmpty() ? 0 : NameBit)
                  | (data.enchantments.isEmpty() ? 0 : EnchantmentsBit));
}

// Writes the components of `data` after their bits, which are not 0.
void writeData(QDataStream &stream, const QcItemData &data);

// Reads a stack whose contents may nest `depth` more levels; a corrupt stream for invalid data.
QcItemStack readStackAt(QDataStream &stream, int depth);

void writeData(QDataStream &stream, const QcItemData &data)
{
    stream << bitsOf(data);
    if (data.mapId != 0) {
        stream << data.mapId;
    }
    if (!data.contents.isEmpty()) {
        stream << static_cast<quint8>(data.contents.size());
        for (const QcItemStack &slot : data.contents) {
            writeStack(stream, slot);
        }
    }
    if (!data.name.isEmpty()) {
        stream << data.name;
    }
    if (!data.enchantments.isEmpty()) {
        stream << static_cast<quint8>(data.enchantments.size());
        for (const QcEnchantment &enchantment : data.enchantments) {
            stream << enchantment.name << static_cast<quint8>(enchantment.level);
        }
    }
}

// Reads the enchantments of a stack (decision E-864); none when they break the limits of QcItemData.
std::optional<QList<QcEnchantment>> readEnchantments(QDataStream &stream)
{
    quint8 count = 0;
    stream >> count;
    if (count == 0 || count > QcItemData::maxEnchantments) {
        return std::nullopt;
    }
    QList<QcEnchantment> enchantments;
    for (quint8 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        QcEnchantment enchantment;
        quint8 level = 0;
        stream >> enchantment.name >> level;
        enchantment.level = level;
        if (enchantment.name.isEmpty() || enchantment.name.size() > QcItemData::maxEnchantmentName || level == 0) {
            return std::nullopt;
        }
        enchantments.append(enchantment);
    }
    return enchantments;
}

// Reads the data after a stack's damage: contents of at most maxContents stacks nesting at most
// `depth` levels and holding at most maxStacksInside stacks, a name of at most maxNameLength
// characters; else the stream turns corrupt.
std::optional<QcItemData> readData(QDataStream &stream, int depth)
{
    quint8 bits = 0;
    stream >> bits;
    QcItemData data;
    if (bits == 0 || (bits & ~KnownBits) != 0) {
        return std::nullopt;
    }
    if ((bits & MapIdBit) != 0) {
        stream >> data.mapId;
    }
    if ((bits & ContentsBit) != 0) {
        quint8 count = 0;
        stream >> count;
        if (depth <= 0 || count > QcItemData::maxContents) {
            return std::nullopt;
        }
        for (quint8 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
            data.contents.append(readStackAt(stream, depth - 1));
        }
        // Each stack inside kept the limit itself, so a level reads at most maxContents of them.
        if (data.stacksInside() > QcItemData::maxStacksInside) {
            return std::nullopt;
        }
    }
    if ((bits & NameBit) != 0) {
        stream >> data.name;
        if (data.name.size() > QcItemData::maxNameLength) {
            return std::nullopt;
        }
    }
    if ((bits & EnchantmentsBit) != 0) {
        std::optional<QList<QcEnchantment>> enchantments = readEnchantments(stream);
        if (!enchantments) {
            return std::nullopt;
        }
        data.enchantments = std::move(*enchantments);
    }
    return data;
}

QcItemStack readStackAt(QDataStream &stream, int depth)
{
    QcItemStack slot;
    qint32 amount = 0;
    qint32 damage = 0;
    stream >> slot.item >> amount >> damage;
    slot.count = amount & ~stackDataFlag;
    slot.damage = damage;
    if ((amount & stackDataFlag) == 0) {
        return slot;
    }
    const std::optional<QcItemData> data = readData(stream, depth);
    if (!data) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return {};
    }
    return data->isEmpty() ? slot : slot.withData(*data);
}

} // namespace

void writeStack(QDataStream &stream, const QcItemStack &stack)
{
    const quint8 bits = bitsOf(stack.itemData());
    stream << stack.item << static_cast<qint32>(bits != 0 ? stack.count | stackDataFlag : stack.count) << static_cast<qint32>(stack.damage);
    if (bits != 0) {
        writeData(stream, stack.itemData());
    }
}

QcItemStack readStack(QDataStream &stream)
{
    return readStackAt(stream, QcItemData::maxDepth);
}

QcItemStack readFlatStack(QDataStream &stream)
{
    return readStackAt(stream, 0);
}

void writeStacks(QDataStream &stream, const QList<QcItemStack> &stacks)
{
    stream << static_cast<quint32>(stacks.size());
    for (const QcItemStack &slot : stacks) {
        writeStack(stream, slot);
    }
}

std::optional<QList<QcItemStack>> readStacks(QDataStream &stream)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, 1024);
    if (!countRead) {
        return std::nullopt;
    }
    const quint32 count = *countRead;
    QList<QcItemStack> stacks;
    stacks.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        stacks.append(readStack(stream));
    }
    return stacks;
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

void writeStandPose(QDataStream &stream, const QcStandPose &pose)
{
    for (const QcStandPose::Rotation &rotation : pose.parts) {
        stream << rotation.x << rotation.y << rotation.z;
    }
    stream << pose.flags();
}

QcStandPose readStandPose(QDataStream &stream)
{
    std::array<QcStandPose::Rotation, QcStandPose::partCount> parts{};
    for (QcStandPose::Rotation &rotation : parts) {
        stream >> rotation.x >> rotation.y >> rotation.z;
    }
    quint8 flags = 0;
    stream >> flags;

    QcStandPose pose = QcStandPose::withFlags(flags);
    pose.parts = parts;
    return pose.normalized();
}

bool isFinite(const QcVec3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace QcProtocolCodec
