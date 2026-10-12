#include "QcGameMode.h"

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
    return mode == QcGameMode::Creative ? "creative"_L1 : "survival"_L1;
}

std::optional<QcGameMode> parse(QStringView text)
{
    const QString lower = text.trimmed().toString().toLower();
    if (lower == "survival"_L1 || lower == "s"_L1 || lower == "0"_L1) {
        return QcGameMode::Survival;
    }
    if (lower == "creative"_L1 || lower == "c"_L1 || lower == "1"_L1) {
        return QcGameMode::Creative;
    }
    return std::nullopt;
}

std::optional<QcGameMode> fromValue(quint8 value)
{
    if (value >= quint8(count)) {
        return std::nullopt;
    }
    return QcGameMode(value);
}

} // namespace QcGameModes
