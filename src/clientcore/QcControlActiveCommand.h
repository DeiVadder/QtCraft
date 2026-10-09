#pragma once

#include <QJsonObject>

#include <functional>

// A control request that completes on a later tick.
struct QcControlActiveCommand
{
    QJsonObject request;
    // Poll intervals left before the command fails with a timeout.
    int remainingTicks = 0;
    // Called every tick; returns true when the command is complete.
    std::function<bool()> step;
    // Response of a completed command; the player status when unset. The initializer lets
    // commands leave it out without -Wmissing-field-initializers.
    std::function<QJsonObject()> result = {}; // NOLINT(readability-redundant-member-init)

    // Tick-based commands fail when the session stops ticking (e.g. while loading) for much longer than asked.
    static constexpr int timeoutPolls(int ticks) { return ticks * 4 + 100; }
};
