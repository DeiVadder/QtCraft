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

// A spot as read; `valid` is false for one outside the world.
struct Spot
{
    std::optional<QcBlockPos> pos;
    bool valid = true;
};

[[nodiscard]] Spot readSpot(QDataStream &stream)
{
    bool present = false;
    stream >> present;
    if (!present) {
        return {};
    }
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    const QcBlockPos pos{x, y, z};
    const bool inside = pos.isInsideWorldHeight() && std::abs(qint64(x)) <= QcWorld::maxHorizontalBlock
                        && std::abs(qint64(z)) <= QcWorld::maxHorizontalBlock;
    return {pos, inside};
}

void writeOffers(QDataStream &stream, const QcVillagerRecord &villager)
{
    stream << quint32(villager.offers.size());
    for (const QcVillagerOffer &offer : villager.offers) {
        stream << offer.id << qint32(offer.uses) << qint32(offer.demand);
    }
    stream << villager.lastRestock << qint32(villager.restocks);
}

// The saved state of a villager's offers (QcVillagerRecord::offers, lastRestock, restocks).
struct Offers
{
    QList<QcVillagerOffer> offers;
    double lastRestock = -1.0;
    int restocks = 0;
};

// std::nullopt for more offers than a villager has, uses or demand out of range or a broken time.
[[nodiscard]] std::optional<Offers> readOffers(QDataStream &stream)
{
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > quint32(QcVillagerRecord::maxOffers)) {
        return std::nullopt;
    }
    Offers read;
    for (quint32 i = 0; i < count; ++i) {
        std::optional<QString> id = QcComponentFrames::readText(stream, maxText);
        if (!id) {
            return std::nullopt;
        }
        qint32 uses = 0;
        qint32 demand = 0;
        stream >> uses >> demand;
        if (uses < 0 || uses > QcTradeOffer::maxUsesLimit || demand < 0 || demand > QcVillagerOffer::maxDemand) {
            return std::nullopt;
        }
        QcVillagerOffer offer;
        offer.id = std::move(*id);
        offer.uses = uses;
        offer.demand = demand;
        read.offers.append(offer);
    }

    qint32 restocks = 0;
    stream >> read.lastRestock >> restocks;
    read.restocks = restocks;
    const bool valid = std::isfinite(read.lastRestock) && read.lastRestock >= -1.0 && read.lastRestock <= QcVillagerRecord::maxRestockTime
                       && restocks >= 0 && restocks <= 64;
    return valid ? std::optional(std::move(read)) : std::nullopt;
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
    const Spot home = readSpot(stream);
    const Spot work = readSpot(stream);
    villager.home = home.pos;
    villager.work = work.pos;
    std::optional<QString> profession = QcComponentFrames::readText(stream, maxText);
    if (!profession) {
        return std::nullopt;
    }
    villager.profession = std::move(*profession);
    stream >> level >> experience >> villager.look;
    std::optional<QString> style = QcComponentFrames::readText(stream, maxText);
    if (!style) {
        return std::nullopt;
    }
    villager.style = std::move(*style);
    stream >> produce;
    // Version 1 has no offers yet: none are traded, it never restocked.
    if (version >= 2) {
        std::optional<Offers> offers = readOffers(stream);
        if (!offers) {
            return std::nullopt;
        }
        villager.offers = std::move(offers->offers);
        villager.lastRestock = offers->lastRestock;
        villager.restocks = offers->restocks;
    }
    // Version 3 knows when it last slept (decision E-470); older ones never slept.
    if (version >= 3) {
        stream >> villager.sleptAt;
    }
    if (stream.status() != QDataStream::Ok || !home.valid || !work.valid || level < 1 || level > QcVillagerRecord::maxLevel || experience < 0
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
