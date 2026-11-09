// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/mobs/QcMobSerializer.h"

#include "common/protocol/QcStreamLimits.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// The boxes of a mob's model; fails on too many or an unknown tint.
[[nodiscard]] std::optional<QList<QcMobPart>> readParts(QDataStream &stream)
{
    const std::optional<quint32> partCount = QcStreamLimits::readCount(stream, 256);
    if (!partCount) {
        return std::nullopt;
    }

    QList<QcMobPart> parts;
    for (quint32 p = 0; p < *partCount; ++p) {
        QcMobPart part;
        quint8 tint = 0;
        stream >> part.from.x >> part.from.y >> part.from.z >> part.to.x >> part.to.y >> part.to.z >> part.texture >> tint >> part.sitOffset.x
            >> part.sitOffset.y >> part.sitOffset.z >> part.tentacle >> part.saddled >> part.armour >> part.spin;
        const bool tinted = tint == static_cast<quint8>(QcTint::None) || tint == static_cast<quint8>(QcTint::Collar);
        if (!tinted || !(std::abs(part.spin) <= QcMobPart::maxSpin)) {
            return std::nullopt;
        }
        part.tint = static_cast<QcTint>(tint);
        parts.append(part);
    }
    return parts;
}

// The pulse of a creature that swims in pulses, within what the parser allows; none when the
// stream says it has none. Fails on values out of range.
[[nodiscard]] std::optional<std::optional<QcPulseDefinition>> readPulse(QDataStream &stream)
{
    bool pulses = false;
    stream >> pulses;
    if (!pulses) {
        return std::optional<QcPulseDefinition>{};
    }
    QcPulseDefinition pulse;
    stream >> pulse.seconds >> pulse.thrust >> pulse.speed >> pulse.spread;
    const auto within = [](double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; };
    if (!within(pulse.seconds, 0.2, 10.0) || !within(pulse.thrust, 0.05, 0.9) || !within(pulse.speed, 0.1, 10.0) || !within(pulse.spread, 0.0, 90.0)) {
        return std::nullopt;
    }
    return std::optional<QcPulseDefinition>{pulse};
}

// The rider's seat of a creature; a negative height stands for none.
std::optional<QcRideDefinition> readRide(QDataStream &stream)
{
    double seat = -1.0;
    stream >> seat;
    if (seat < 0.0) {
        return std::nullopt;
    }
    QcRideDefinition ride;
    ride.seat = seat;
    return ride;
}

} // namespace

namespace QcMobSerializer {

void writeMob(QDataStream &stream, const QcMobDefinition &mob)
{
    stream << mob.name << mob.translationKey << mob.maxHealth << mob.width << mob.height << mob.hostile
           << static_cast<quint32>(mob.parts.size());
    for (const QcMobPart &part : mob.parts) {
        stream << part.from.x << part.from.y << part.from.z << part.to.x << part.to.y << part.to.z << part.texture << static_cast<quint8>(part.tint)
               << part.sitOffset.x << part.sitOffset.y << part.sitOffset.z << part.tentacle << part.saddled << part.armour << part.spin;
    }
    stream << mob.skeleton << mob.outfit << mob.cape << mob.armsForward << mob.villager << mob.ambientSound << mob.hurtSound << mob.deathSound
           << mob.flying << static_cast<qint32>(mob.glow) << mob.catchWith << mob.catchItem << static_cast<quint32>(mob.breeding.food.size());
    for (const QString &food : mob.breeding.food) {
        stream << food;
    }
    // Every use of a tameable creature goes to the server (decision E-480).
    stream << mob.tameable.has_value();
    // Its milking item is used on it (decision E-500).
    stream << mob.milkWith;
    // The thread from its children to it (decision E-839): the child kind, texture and reach; the
    // rest of the family stays on the server.
    const std::optional<QcCreatureKindId> child = mob.family && !mob.family->tetherTexture.isEmpty() ? mob.family->childType : std::nullopt;
    stream << child.has_value();
    if (child && mob.family) {
        stream << static_cast<qint32>(child->index()) << mob.family->tetherTexture << mob.family->tetherRange;
    }
    // Swimming in pulses and the ink cloud (decision E-974).
    stream << mob.pulse.has_value();
    if (mob.pulse) {
        stream << mob.pulse->seconds << mob.pulse->thrust << mob.pulse->speed << mob.pulse->spread;
    }
    stream << mob.ink;
    stream << (mob.ride ? mob.ride->seat : -1.0);
}

std::optional<QcMobDefinition> readMob(QDataStream &stream)
{
    QcMobDefinition mob;

    stream >> mob.name >> mob.translationKey >> mob.maxHealth >> mob.width >> mob.height >> mob.hostile;
    std::optional<QList<QcMobPart>> parts = readParts(stream);
    if (!parts) {
        return std::nullopt;
    }
    mob.parts = std::move(*parts);

    qint32 glow = 0;
    stream >> mob.skeleton >> mob.outfit >> mob.cape >> mob.armsForward >> mob.villager >> mob.ambientSound >> mob.hurtSound >> mob.deathSound
        >> mob.flying >> glow >> mob.catchWith >> mob.catchItem;
    mob.glow = std::clamp(static_cast<int>(glow), 0, QcMobDefinition::maxGlow);
    // What it eats for breeding, so the client knows to use it on the creature (decision E-293).
    const std::optional<quint32> foodCountRead = QcStreamLimits::readCount(stream, 64);
    if (!foodCountRead) {
        return std::nullopt;
    }
    const quint32 foodCount = *foodCountRead;
    for (quint32 f = 0; f < foodCount; ++f) {
        QString food;
        stream >> food;
        mob.breeding.food.append(food);
    }
    bool tameable = false;
    stream >> tameable;
    if (tameable) {
        mob.tameable.emplace();
    }
    stream >> mob.milkWith;
    bool tethers = false;
    stream >> tethers;
    if (tethers) {
        QcFamilyDefinition &family = mob.family.emplace();
        qint32 child = -1;
        stream >> child >> family.tetherTexture >> family.tetherRange;
        // The table holds at most maxDefinitions creatures; an index below that but beyond the
        // creatures read never matches one (QcTetherMesher, mobs().contains()).
        if (child < 0 || std::cmp_greater_equal(child, QcStreamLimits::maxDefinitions) || family.tetherTexture.isEmpty() || !std::isfinite(family.tetherRange) || family.tetherRange <= 0.0 || family.tetherRange > 64.0) {
            return std::nullopt;
        }
        family.childType = QcCreatureKindId{child};
    }
    const std::optional<std::optional<QcPulseDefinition>> pulse = readPulse(stream);
    if (!pulse) {
        return std::nullopt;
    }
    mob.pulse = *pulse;
    stream >> mob.ink;
    mob.ride = readRide(stream);

    // Sizes feed physics and the mesher: reject anything outside what the parser allows.
    const auto plausible = [](double value) { return std::isfinite(value) && std::abs(value) <= 64.0; };
    bool valid = std::isfinite(mob.maxHealth) && mob.maxHealth > 0.0 && mob.maxHealth <= 1000.0 && plausible(mob.width) && plausible(mob.height) && mob.width > 0.0
                 && mob.height > 0.0;
    valid = valid && (!mob.ride || mob.ride->seat <= 8.0);
    for (const QcMobPart &part : std::as_const(mob.parts)) {
        valid = valid && plausible(part.from.x) && plausible(part.from.y) && plausible(part.from.z)
                && plausible(part.to.x) && plausible(part.to.y) && plausible(part.to.z) && part.sitOffset.length() <= 1.0;
    }
    if (stream.status() != QDataStream::Ok || !valid) {
        return std::nullopt;
    }
    return mob;
}

} // namespace QcMobSerializer
