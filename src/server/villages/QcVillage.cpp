// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/villages/QcVillage.h"

#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int maxPopulation = 1 << 16;
constexpr double maxWorldTime = 1e15;

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
    if (!pos.isInsideWorldHeight() || std::abs(static_cast<qint64>(pos.x)) > QcWorld::maxChunkBlock
        || std::abs(static_cast<qint64>(pos.z)) > QcWorld::maxChunkBlock) {
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
        // As a string: JSON numbers cannot hold every 64-bit id exactly.
        if (spot.claimedBy != 0) {
            entry.insert(u"claimedBy"_s, QString::number(spot.claimedBy));
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
        // A claim that is no number is no claim (the spot is free).
        const quint64 claimedBy = entry.toObject().value(u"claimedBy"_s).toString().toULongLong();
        spots.append({*at, entry.toObject().value(u"kind"_s).toString().left(64), claimedBy});
    }
    return spots;
}

} // namespace

qsizetype QcVillage::freeBeds() const
{
    return std::ranges::count_if(beds, [](const Spot &bed) { return bed.claimedBy == 0; });
}

QJsonObject QcVillage::toJson() const
{
    QJsonObject json{{u"id"_s, QString::number(id)},
            {u"style"_s, style},
            {u"centre"_s, position(centre)},
            {u"min"_s, position({minX, minY, minZ})},
            {u"max"_s, position({maxX, maxY, maxZ})},
            {u"beds"_s, spotsJson(beds)},
            {u"workstations"_s, spotsJson(workstations)},
            {u"population"_s, population},
            {u"golems"_s, golems}};
    if (lastGolem >= 0.0) {
        json.insert(u"lastGolem"_s, lastGolem);
    }
    return json;
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
    // A world time beyond about 30 million years of play counts as damaged: no golem yet.
    const double lastGolem = json.value(u"lastGolem"_s).toDouble(-1.0);
    village.lastGolem = std::isfinite(lastGolem) && lastGolem >= 0.0 && lastGolem <= maxWorldTime ? lastGolem : -1.0;
    return village;
}
