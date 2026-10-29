// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcEntityKind.h"

namespace QcEntityKinds {

[[nodiscard]] std::optional<QcEntityKind> fromValue(quint8 value)
{
    if (value >= count) {
        return std::nullopt;
    }
    return static_cast<QcEntityKind>(value);
}

QLatin1StringView name(QcEntityKind kind)
{
    switch (kind) {
    case QcEntityKind::Player:
        return QLatin1StringView("player");
    case QcEntityKind::Creature:
        return QLatin1StringView("creature");
    case QcEntityKind::Item:
        return QLatin1StringView("item");
    case QcEntityKind::FallingBlock:
        return QLatin1StringView("falling block");
    case QcEntityKind::ArmourStand:
        return QLatin1StringView("armour stand");
    case QcEntityKind::MovingBlock:
        return QLatin1StringView("moving block");
    case QcEntityKind::Minecart:
        return QLatin1StringView("minecart");
    case QcEntityKind::ExperienceOrb:
        return QLatin1StringView("experience orb");
    case QcEntityKind::Projectile:
        return QLatin1StringView("projectile");
    case QcEntityKind::PrimedExplosive:
        return QLatin1StringView("primed explosive");
    case QcEntityKind::Hanging:
        return QLatin1StringView("hanging");
    case QcEntityKind::Boat:
        return QLatin1StringView("boat");
    }
    return QLatin1StringView("unknown");
}

} // namespace QcEntityKinds
