#include "common/content/registry/QcMobSerializer.h"

#include "common/protocol/QcStreamLimits.h"

#include <algorithm>
#include <cmath>

namespace QcMobSerializer {

void writeMob(QDataStream &stream, const QcMobDefinition &mob)
{
    stream << mob.name << mob.translationKey << mob.maxHealth << mob.width << mob.height << mob.hostile
           << quint32(mob.parts.size());
    for (const QcMobPart &part : mob.parts) {
        stream << part.from.x << part.from.y << part.from.z << part.to.x << part.to.y << part.to.z << part.texture << quint8(part.tint)
               << part.sitOffset.x << part.sitOffset.y << part.sitOffset.z;
    }
    stream << mob.skeleton << mob.outfit << mob.cape << mob.armsForward << mob.villager << mob.ambientSound << mob.hurtSound << mob.deathSound
           << mob.flying << qint32(mob.glow) << mob.catchWith << mob.catchItem << quint32(mob.breeding.food.size());
    for (const QString &food : mob.breeding.food) {
        stream << food;
    }
    // Every use of a tameable creature goes to the server (decision E-480).
    stream << mob.tameable.has_value();
    // Its milking item is used on it (decision E-500).
    stream << mob.milkWith;
    // The thread from its children to it (decision E-839): the child kind, texture and reach; the
    // rest of the family stays on the server.
    const bool tethers = mob.family && mob.family->childType >= 0 && !mob.family->tetherTexture.isEmpty();
    stream << tethers;
    if (tethers) {
        stream << qint32(mob.family->childType) << mob.family->tetherTexture << mob.family->tetherRange;
    }
}

std::optional<QcMobDefinition> readMob(QDataStream &stream)
{
    QcMobDefinition mob;

    stream >> mob.name >> mob.translationKey >> mob.maxHealth >> mob.width >> mob.height >> mob.hostile;
    const std::optional<quint32> partCountRead = QcStreamLimits::readCount(stream, 256);
    if (!partCountRead) {
        return std::nullopt;
    }
    const quint32 partCount = *partCountRead;

    for (quint32 p = 0; p < partCount; ++p) {
        QcMobPart part;
        quint8 tint = 0;
        stream >> part.from.x >> part.from.y >> part.from.z >> part.to.x >> part.to.y >> part.to.z >> part.texture >> tint >> part.sitOffset.x
            >> part.sitOffset.y >> part.sitOffset.z;
        if (tint != quint8(QcTint::None) && tint != quint8(QcTint::Collar)) {
            return std::nullopt;
        }
        part.tint = QcTint(tint);
        mob.parts.append(part);
    }

    qint32 glow = 0;
    stream >> mob.skeleton >> mob.outfit >> mob.cape >> mob.armsForward >> mob.villager >> mob.ambientSound >> mob.hurtSound >> mob.deathSound
        >> mob.flying >> glow >> mob.catchWith >> mob.catchItem;
    mob.glow = std::clamp(int(glow), 0, QcMobDefinition::maxGlow);
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
        // A child index beyond the creatures never matches one (QcTetherMesher).
        family.childType = child;
        if (child < 0 || family.tetherTexture.isEmpty() || !std::isfinite(family.tetherRange) || family.tetherRange <= 0.0 || family.tetherRange > 64.0) {
            return std::nullopt;
        }
    }

    // Sizes feed physics and the mesher: reject anything outside what the parser allows.
    const auto plausible = [](double value) { return std::isfinite(value) && std::abs(value) <= 64.0; };
    bool valid = std::isfinite(mob.maxHealth) && mob.maxHealth > 0.0 && mob.maxHealth <= 1000.0 && plausible(mob.width) && plausible(mob.height) && mob.width > 0.0
                 && mob.height > 0.0;
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
