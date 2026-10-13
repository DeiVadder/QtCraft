#include "QcServerLimits.h"

#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QcServerLimits::Field, 7> table{{
    {"signalBudget", "Signal Dust work per tick", 256, qint64(1) << 22, &QcServerLimits::signalBudget},
    {"signalScheduleReservePercent", "share of the signal budget kept for due scheduled ticks", 0, 90,
     &QcServerLimits::signalScheduleReservePercent},
    {"signalWorkPerChange", "work one block change of a circuit step counts for", 1, 64, &QcServerLimits::signalWorkPerChange},
    {"signalMinimumStep", "changes a circuit step takes at least", 1, 65536, &QcServerLimits::signalMinimumStep},
    {"signalMaxPendingChanges", "block changes near circuits waiting at most", 1024, qint64(1) << 24,
     &QcServerLimits::signalMaxPendingChanges},
    {"hopperBudget", "Signal Hoppers acting per tick", 1, qint64(1) << 20, &QcServerLimits::hopperBudget},
    {"hopperCooldownTicks", "ticks a hopper rests after moving an item", 1, 1200, &QcServerLimits::hopperCooldownTicks},
}};

} // namespace

std::span<const QcServerLimits::Field> QcServerLimits::fields()
{
    return table;
}

QcServerLimits QcServerLimits::fromJson(const QJsonObject &json, const QString &source, QStringList *warnings)
{
    QcServerLimits limits;
    const auto warn = [&](const QString &text) {
        if (warnings) {
            warnings->append(u"%1: limits: %2"_s.arg(source, text));
        }
    };

    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        const auto field = std::ranges::find_if(table, [&it](const Field &candidate) { return it.key() == QLatin1StringView(candidate.key); });
        if (field == table.end()) {
            warn(u"unknown key \"%1\" ignored"_s.arg(it.key()));
            continue;
        }
        const double number = it.value().toDouble(std::nan(""));
        if (!it.value().isDouble() || !std::isfinite(number)) {
            warn(u"\"%1\" is no number, the default %2 stays"_s.arg(it.key()).arg(limits.*(field->value)));
            continue;
        }
        const qint64 wanted = qint64(std::llround(std::clamp(number, -9.0e15, 9.0e15)));
        const qint64 clamped = std::clamp(wanted, field->low, field->high);
        if (clamped != wanted || number != std::floor(number)) {
            warn(u"\"%1\" = %2 is outside %3..%4 (whole numbers), using %5"_s.arg(it.key()).arg(number).arg(field->low).arg(field->high).arg(clamped));
        }
        limits.*(field->value) = clamped;
    }
    return limits;
}

QJsonObject QcServerLimits::toJson() const
{
    QJsonObject json;
    for (const Field &field : table) {
        json.insert(QLatin1StringView(field.key), double(this->*(field.value)));
    }
    return json;
}

QStringList QcServerLimits::lines() const
{
    QStringList lines;
    for (const Field &field : table) {
        lines.append(u"%1 = %2 (%3..%4): %5"_s.arg(QLatin1StringView(field.key)).arg(this->*(field.value)).arg(field.low).arg(field.high)
                         .arg(QLatin1StringView(field.description)));
    }
    return lines;
}
