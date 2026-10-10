#pragma once

#include "QcCommandRegistry.h"

#include <functional>
#include <optional>

class QcTickControl;
class QcTickPhaseTimer;
class QcTickScheduler;
struct QcServerState;

// /tick (decision E-249): freezing the world, single steps, the tick rate, sprints and a query.
// Rate and freezing are not saved; a world always opens at 20 ticks per second, running.
class QcTickCommands
{
public:
    // Most ticks of one sprint (12 hours of game time).
    static constexpr quint64 maxSprintTicks = 864'000;

    struct Parts
    {
        QcTickScheduler &ticks;
        QcTickControl &control;
        const QcTickPhaseTimer &phases;
        // The speed of the time of day changed: the clients get the time again.
        std::function<void()> timeChanged;
    };

    QcTickCommands(QcServerState &state, Parts parts);

    void addTo(QcCommandRegistry &registry);
    // Called by the server after its due ticks: reports a sprint that ended to the log and the
    // operators online.
    void reportSprintEnd();

private:
    void run(const QcCommandContext &context);
    void freeze(const QcCommandContext &context, bool frozen);
    void step(const QcCommandContext &context);
    void rate(const QcCommandContext &context);
    void sprint(const QcCommandContext &context);
    void query(const QcCommandContext &context) const;
    // "<n>" ticks or "<n>s", "<n>min", "<n>h", "<n>d" (days of 600 s) of game time.
    static std::optional<quint64> parseDuration(const QString &word);

    QcServerState &m_state;
    Parts m_parts;
};
