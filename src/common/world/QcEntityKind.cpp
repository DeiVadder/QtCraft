#include "QcEntityKind.h"

namespace QcEntityKinds {

[[nodiscard]] std::optional<QcEntityKind> fromValue(quint8 value)
{
    if (value >= count) {
        return std::nullopt;
    }
    return QcEntityKind(value);
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
    }
    return QLatin1StringView("unknown");
}

} // namespace QcEntityKinds
