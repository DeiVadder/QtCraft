#include "QcServerLimits.h"

#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QcServerLimits::Field, 32> table{{
    {"signalBudget", "Signal Dust work per tick", 256, qint64(1) << 22, &QcServerLimits::signalBudget},
    {"signalScheduleReservePercent", "share of the signal budget kept for due scheduled ticks", 0, 90,
     &QcServerLimits::signalScheduleReservePercent},
    {"signalWorkPerChange", "work one block change of a circuit step counts for", 1, 64, &QcServerLimits::signalWorkPerChange},
    {"signalMinimumStep", "changes a circuit step takes at least", 1, 65536, &QcServerLimits::signalMinimumStep},
    {"signalMaxPendingChanges", "block changes near circuits waiting at most", 1024, qint64(1) << 24,
     &QcServerLimits::signalMaxPendingChanges},
    {"hopperBudget", "Signal Hoppers acting per tick", 1, qint64(1) << 20, &QcServerLimits::hopperBudget},
    {"hopperCooldownTicks", "ticks a hopper rests after moving an item", 1, 1200, &QcServerLimits::hopperCooldownTicks},
    {"summonLimit", "awake creatures summoned by /summon (0: none)", 0, 4096, &QcServerLimits::summonLimit},
    {"maxContainers", "containers (chests, barrels, hoppers, dispensers) in the world", 16, 1 << 20, &QcServerLimits::maxContainers},
    {"maxMachines", "machines (furnaces) in the world", 16, 1 << 18, &QcServerLimits::maxMachines},
    {"maxSaplings", "growing saplings in the world", 16, 1 << 18, &QcServerLimits::maxSaplings},
    {"blockUpdatesPerTick", "neighbour updates per tick", 64, 1 << 20, &QcServerLimits::blockUpdatesPerTick},
    {"waitingBlockUpdates", "neighbour updates waiting for unloaded chunks", 1024, 1 << 24, &QcServerLimits::waitingBlockUpdates},
    {"commandBlockUpdates", "neighbour updates queued by commands", 1024, 1 << 24, &QcServerLimits::commandBlockUpdates},
    {"savedBlockUpdates", "pending neighbour updates written to a save", 1024, qint64(4) << 20, &QcServerLimits::savedBlockUpdates},
    {"liquidStepsPerTick", "flow steps of liquids per tick", 16, 1 << 20, &QcServerLimits::liquidStepsPerTick},
    {"liquidPlannedSteps", "flow steps of liquids planned", 1024, 1 << 24, &QcServerLimits::liquidPlannedSteps},
    {"fallingBlocks", "falling blocks at once", 16, 65536, &QcServerLimits::fallingBlocks},
    {"itemsAwake", "dropped items awake in loaded chunks", 16, 65536, &QcServerLimits::itemsAwake},
    {"itemsQueued", "dropped items waiting to appear", 16, 1 << 20, &QcServerLimits::itemsQueued},
    {"itemsTotal", "dropped items in the world", 64, 1 << 20, &QcServerLimits::itemsTotal},
    {"hostileCreaturesPerPlayer", "hostile creatures spawning per player", 0, 256, &QcServerLimits::hostileCreaturesPerPlayer},
    {"passiveCreaturesPerPlayer", "passive creatures spawning per player", 0, 256, &QcServerLimits::passiveCreaturesPerPlayer},
    {"ambientCreaturesPerPlayer", "ambient creatures spawning per player", 0, 256, &QcServerLimits::ambientCreaturesPerPlayer},
    {"breedingCrowd", "animals of a kind within 3 x 3 chunks that still breed", 2, 1024, &QcServerLimits::breedingCrowd},
    {"bredPerKind", "bred animals of a kind in the world", 0, 65536, &QcServerLimits::bredPerKind},
    {"randomTicksPerSection", "random ticks per marked section and tick", 0, 64, &QcServerLimits::randomTicksPerSection},
    {"leafDecaysPerTick", "leaves decaying per tick", 1, 4096, &QcServerLimits::leafDecaysPerTick},
    {"leafChecksPerTick", "leaves checked per tick", 1, 65536, &QcServerLimits::leafChecksPerTick},
    {"leafPendingChecks", "leaf checks waiting", 256, 1 << 20, &QcServerLimits::leafPendingChecks},
    {"maxArmourStands", "armour stands in the world", 16, 65536, &QcServerLimits::maxArmourStands},
    {"movingBlocksShown", "blocks shown sliding behind pistons", 16, 65536, &QcServerLimits::movingBlocksShown},
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
