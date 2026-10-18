#include "server/storage/QcVillagerCodec.h"

#include "common/core/QcWorldConstants.h"
#include "server/storage/QcComponentFrames.h"

#include <cmath>
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

void writeOffers(QDataStream &stream, const QcVillagerRecord &villager)
{
    stream << quint32(villager.offers.size());
    for (const QcVillagerOffer &offer : villager.offers) {
        stream << offer.id << qint32(offer.uses) << qint32(offer.demand);
    }
    stream << villager.lastRestock << qint32(villager.restocks);
}

// False for more offers than a villager has, uses or demand out of range or a broken time.
bool readOffers(QDataStream &stream, QcVillagerRecord &villager)
{
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > quint32(QcVillagerRecord::maxOffers)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        QcVillagerOffer offer;
        qint32 uses = 0;
        qint32 demand = 0;
        if (!QcComponentFrames::readText(stream, offer.id, maxText)) {
            return false;
        }
        stream >> uses >> demand;
        if (uses < 0 || uses > QcTradeOffer::maxUsesLimit || demand < 0 || demand > QcVillagerOffer::maxDemand) {
            return false;
        }
        offer.uses = uses;
        offer.demand = demand;
        villager.offers.append(offer);
    }

    qint32 restocks = 0;
    stream >> villager.lastRestock >> restocks;
    villager.restocks = restocks;
    return std::isfinite(villager.lastRestock) && villager.lastRestock >= -1.0 && villager.lastRestock <= QcVillagerRecord::maxRestockTime
           && restocks >= 0 && restocks <= 64;
}

} // namespace

namespace QcVillagerCodec {

void write(QDataStream &stream, const QcVillagerRecord &villager)
{
    stream << villager.village;
    writeSpot(stream, villager.home);
    writeSpot(stream, villager.work);
    stream << villager.profession << qint32(villager.level) << qint32(villager.experience) << villager.look << villager.style
           << qint32(villager.produce);
    writeOffers(stream, villager);
    stream << villager.sleptAt;
}

std::optional<QcVillagerRecord> read(QDataStream &stream, quint16 version)
{
    QcVillagerRecord villager;
    qint32 level = 0;
    qint32 experience = 0;
    qint32 produce = 0;
    // Texts are checked before they are read (QcComponentFrames::readText), every other field after.
    stream >> villager.village;
    const bool spots = readSpot(stream, villager.home) && readSpot(stream, villager.work);
    if (!QcComponentFrames::readText(stream, villager.profession, maxText)) {
        return std::nullopt;
    }
    stream >> level >> experience >> villager.look;
    if (!QcComponentFrames::readText(stream, villager.style, maxText)) {
        return std::nullopt;
    }
    stream >> produce;
    // Version 1 has no offers yet: none are traded, it never restocked.
    if (version >= 2 && !readOffers(stream, villager)) {
        return std::nullopt;
    }
    // Version 3 knows when it last slept (decision E-470); older ones never slept.
    if (version >= 3) {
        stream >> villager.sleptAt;
    }
    if (stream.status() != QDataStream::Ok || !spots || level < 1 || level > QcVillagerRecord::maxLevel || experience < 0
        || experience > QcVillagerRecord::maxExperience || produce < 0 || produce > QcVillagerRecord::maxProduce
        || !(villager.sleptAt <= QcVillagerRecord::maxRestockTime)) {
        return std::nullopt;
    }
    villager.level = level;
    villager.experience = experience;
    villager.produce = produce;
    return villager;
}

} // namespace QcVillagerCodec
