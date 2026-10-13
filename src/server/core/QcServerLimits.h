#pragma once

#include <QJsonObject>
#include <QStringList>

#include <span>

// Budgets and bounds of the server's simulation that an operator may tune (decision E-349): read
// from the object "limits" of server.json on a dedicated server, of world.json in a single-player
// world (a dedicated server ignores the world's). Every value has a default and a valid range; a
// value out of range is clamped with a warning, an unknown key is reported and ignored. New limits
// are one line in fields().
struct QcServerLimits
{
    struct Field
    {
        const char *key;
        // What it bounds, for /limits and the documentation.
        const char *description;
        qint64 low;
        qint64 high;
        qint64 QcServerLimits::*value;
    };

    // Signal Dust: work per tick (dust looked at, components, pushed blocks; E-335).
    qint64 signalBudget = 8192;
    // Share of the budget (percent) that block changes leave for due scheduled ticks.
    qint64 signalScheduleReservePercent = 25;
    // A step takes at most the budget left divided by this (the work of one change), ...
    qint64 signalWorkPerChange = 4;
    // ... but at least this many changes, so a tick always gets on.
    qint64 signalMinimumStep = 64;
    // Block changes near circuits waiting at most; further ones are dropped with a warning.
    qint64 signalMaxPendingChanges = qint64(1) << 20;
    // Signal Hoppers acting per tick at most (E-344).
    qint64 hopperBudget = 4096;
    // Ticks a hopper rests after moving an item (the reference's 8).
    qint64 hopperCooldownTicks = 8;

    static std::span<const Field> fields();
    // The defaults with the values of `json` (clamped); `warnings` gets what was clamped or unknown,
    // prefixed with `source` (e.g. "server.json").
    static QcServerLimits fromJson(const QJsonObject &json, const QString &source, QStringList *warnings);
    QJsonObject toJson() const;
    // "key = value (range low..high): description" per limit.
    QStringList lines() const;

    qint64 scheduleReserve() const { return signalBudget * signalScheduleReservePercent / 100; }
    bool operator==(const QcServerLimits &) const = default;
};
