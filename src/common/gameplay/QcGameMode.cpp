#include "common/gameplay/QcGameMode.h"

#include <QString>

using namespace Qt::StringLiterals;

namespace QcGameModes {

Rules rules(QcGameMode mode)
{
    switch (mode) {
    case QcGameMode::Creative:
        return {.mayFly = true,
                .instantBreak = true,
                .infiniteItems = true,
                .invulnerable = true,
                .hungerFrozen = true,
                .itemCatalogue = true};
    case QcGameMode::Adventure:
        return {.buildByAllowList = true};
    case QcGameMode::Spectator:
        return {.mayFly = true,
                .invulnerable = true,
                .hungerFrozen = true,
                .noClip = true,
                .interacts = false,
                .tangible = false};
    case QcGameMode::Survival:
        break;
    }
    return {};
}

int inventorySet(QcGameMode mode)
{
    return mode == QcGameMode::Creative ? 1 : 0;
}

QLatin1StringView name(QcGameMode mode)
{
    switch (mode) {
    case QcGameMode::Creative:
        return "creative"_L1;
    case QcGameMode::Adventure:
        return "adventure"_L1;
    case QcGameMode::Spectator:
        return "spectator"_L1;
    case QcGameMode::Survival:
        break;
    }
    return "survival"_L1;
}

[[nodiscard]] std::optional<QcGameMode> parse(QStringView text)
{
    const QString lower = text.trimmed().toString().toLower();
    if (lower == "survival"_L1 || lower == "s"_L1 || lower == "0"_L1) {
        return QcGameMode::Survival;
    }
    if (lower == "creative"_L1 || lower == "c"_L1 || lower == "1"_L1) {
        return QcGameMode::Creative;
    }
    if (lower == "adventure"_L1 || lower == "a"_L1 || lower == "2"_L1) {
        return QcGameMode::Adventure;
    }
    if (lower == "spectator"_L1 || lower == "sp"_L1 || lower == "3"_L1) {
        return QcGameMode::Spectator;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QcGameMode> fromValue(quint8 value)
{
    switch (static_cast<QcGameMode>(value)) {
    case QcGameMode::Survival:
    case QcGameMode::Creative:
    case QcGameMode::Adventure:
    case QcGameMode::Spectator:
        return static_cast<QcGameMode>(value);
    }
    return std::nullopt;
}

} // namespace QcGameModes
