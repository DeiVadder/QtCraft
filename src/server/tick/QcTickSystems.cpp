#include "server/tick/QcTickSystems.h"

#include "server/core/QcServerLogging.h"
#include "server/tick/QcTickPhaseTimer.h"

void QcTickSystems::add(QcTickPhase phase, When when, std::function<void()> run)
{
    if (phase == QcTickPhase::Messages || !run) {
        qCWarning(lcServer) << "Tick system refused: phase" << QcTickPhases::id(phase) << (run ? "is not part of the tick" : "has no entry point");
        return;
    }

    m_systems.push_back({phase, when, std::move(run)});
}

void QcTickSystems::run(bool simulating, QcTickPhaseTimer &timer) const
{
    for (const System &system : m_systems) {
        if (system.when == When::Simulating && !simulating) {
            continue;
        }

        system.run();
        timer.lap(system.phase);
    }
}
