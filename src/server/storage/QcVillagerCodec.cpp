#include "QcVillagerCodec.h"

#include "QcWorldConstants.h"

#include <QtEndian>

#include <cstdlib>

namespace {

constexpr qsizetype maxText = 128;

void writeSpot(QDataStream &stream, std::optional<QcBlockPos> spot)
{
    stream << spot.has_value();
    if (spot) {
        stream << qint32(spot->x) << qint32(spot->y) << qint32(spot->z);
    }
}

// False for a spot outside the world.
bool readSpot(QDataStream &stream, std::optional<QcBlockPos> &spot)
{
    bool present = false;
    stream >> present;
    if (!present) {
        spot.reset();
        return true;
    }
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    const QcBlockPos pos{x, y, z};
    spot = pos;
    return pos.isInsideWorldHeight() && std::abs(qint64(x)) <= QcWorld::maxHorizontalBlock && std::abs(qint64(z)) <= QcWorld::maxHorizontalBlock;
}

// Reads a QString written by QDataStream (byte length, then UTF-16) after checking its length: a
// damaged length of up to 4 GB is never allocated. False for a longer text (the stream turns
// corrupt).
bool readText(QDataStream &stream, QString &text)
{
    constexpr quint32 null = 0xffffffffu;
    quint32 bytes = 0;
    stream >> bytes;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    if (bytes == null) {
        text = QString();
        return true;
    }
    if (bytes % 2 != 0 || bytes > quint32(maxText) * 2) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }

    QByteArray raw(qsizetype(bytes), Qt::Uninitialized);
    if (stream.readRawData(raw.data(), int(bytes)) != int(bytes)) {
        stream.setStatus(QDataStream::ReadPastEnd);
        return false;
    }
    text.resize(qsizetype(bytes / 2));
    if (stream.byteOrder() == QDataStream::BigEndian) {
        qFromBigEndian<char16_t>(raw.constData(), text.size(), text.data());
    } else {
        qFromLittleEndian<char16_t>(raw.constData(), text.size(), text.data());
    }
    return true;
}

} // namespace

namespace QcVillagerCodec {

void write(QDataStream &stream, const QcVillagerRecord &villager)
{
    stream << villager.village << villager.name;
    writeSpot(stream, villager.home);
    writeSpot(stream, villager.work);
    stream << villager.profession << qint32(villager.level) << qint32(villager.experience) << villager.look << villager.style
           << qint32(villager.produce);
}

std::optional<QcVillagerRecord> read(QDataStream &stream)
{
    QcVillagerRecord villager;
    qint32 level = 0;
    qint32 experience = 0;
    qint32 produce = 0;
    // Texts are checked before they are read (readText), every other field after.
    stream >> villager.village;
    if (!readText(stream, villager.name)) {
        return std::nullopt;
    }
    const bool spots = readSpot(stream, villager.home) && readSpot(stream, villager.work);
    if (!readText(stream, villager.profession)) {
        return std::nullopt;
    }
    stream >> level >> experience >> villager.look;
    if (!readText(stream, villager.style)) {
        return std::nullopt;
    }
    stream >> produce;
    if (stream.status() != QDataStream::Ok || !spots || level < 1 || level > QcVillagerRecord::maxLevel || experience < 0
        || experience > QcVillagerRecord::maxExperience || produce < 0 || produce > QcVillagerRecord::maxProduce) {
        return std::nullopt;
    }
    villager.level = level;
    villager.experience = experience;
    villager.produce = produce;
    return villager;
}

} // namespace QcVillagerCodec
