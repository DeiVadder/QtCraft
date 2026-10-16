#include "QcVillage.h"

#include "QcWorldConstants.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int maxPopulation = 1 << 16;

QJsonArray position(QcBlockPos pos)
{
    return {pos.x, pos.y, pos.z};
}

[[nodiscard]] std::optional<QcBlockPos> readPosition(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 3 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble()) {
        return std::nullopt;
    }
    const QcBlockPos pos{array[0].toInt(), array[1].toInt(), array[2].toInt()};
    if (!pos.isInsideWorldHeight() || std::abs(qint64(pos.x)) > QcWorld::maxHorizontalBlock
        || std::abs(qint64(pos.z)) > QcWorld::maxHorizontalBlock) {
        return std::nullopt;
    }
    return pos;
}

QJsonArray spotsJson(const QList<QcVillage::Spot> &spots)
{
    QJsonArray array;
    for (const QcVillage::Spot &spot : spots) {
        QJsonObject entry{{u"at"_s, position(spot.at)}};
        if (!spot.kind.isEmpty()) {
            entry.insert(u"kind"_s, spot.kind);
        }
        if (!spot.claimedBy.isEmpty()) {
            entry.insert(u"claimedBy"_s, spot.claimedBy);
        }
        array.append(entry);
    }
    return array;
}

// Spots inside the village's area; others are left out.
QList<QcVillage::Spot> readSpots(const QJsonValue &value, const QcVillage &village, qsizetype maxSpots)
{
    QList<QcVillage::Spot> spots;
    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        const std::optional<QcBlockPos> at = readPosition(entry.toObject().value(u"at"_s));
        if (spots.size() >= maxSpots || !at || !village.contains(*at)) {
            continue;
        }
        spots.append({*at, entry.toObject().value(u"kind"_s).toString().left(64), entry.toObject().value(u"claimedBy"_s).toString().left(64)});
    }
    return spots;
}

} // namespace

qsizetype QcVillage::freeBeds() const
{
    return std::ranges::count_if(beds, [](const Spot &bed) { return bed.claimedBy.isEmpty(); });
}

QJsonObject QcVillage::toJson() const
{
    return {{u"id"_s, QString::number(id)},
            {u"style"_s, style},
            {u"centre"_s, position(centre)},
            {u"min"_s, position({minX, minY, minZ})},
            {u"max"_s, position({maxX, maxY, maxZ})},
            {u"beds"_s, spotsJson(beds)},
            {u"workstations"_s, spotsJson(workstations)},
            {u"population"_s, population},
            {u"golems"_s, golems}};
}

std::optional<QcVillage> QcVillage::fromJson(const QJsonObject &json, const ReadLimits &limits)
{
    QcVillage village;
    bool numeric = false;
    village.id = json.value(u"id"_s).toString().toLongLong(&numeric);
    const std::optional<QcBlockPos> centre = readPosition(json.value(u"centre"_s));
    const std::optional<QcBlockPos> low = readPosition(json.value(u"min"_s));
    const std::optional<QcBlockPos> high = readPosition(json.value(u"max"_s));
    if (!numeric || !centre || !low || !high || low->x > high->x || low->y > high->y || low->z > high->z || high->x - low->x >= limits.extent
        || high->z - low->z >= limits.extent) {
        return std::nullopt;
    }

    village.style = json.value(u"style"_s).toString().left(128);
    village.centre = *centre;
    village.minX = low->x;
    village.minY = low->y;
    village.minZ = low->z;
    village.maxX = high->x;
    village.maxY = high->y;
    village.maxZ = high->z;
    village.beds = readSpots(json.value(u"beds"_s), village, limits.beds);
    village.workstations = readSpots(json.value(u"workstations"_s), village, limits.workstations);
    village.population = std::clamp(json.value(u"population"_s).toInt(), 0, maxPopulation);
    village.golems = std::clamp(json.value(u"golems"_s).toInt(), 0, maxPopulation);
    return village;
}
