// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/tick/QcTickSystems.h"

#include "server/core/QcServerLogging.h"
#include "server/tick/QcTickPhaseTimer.h"

void QcTickSystems::add(QcTickPhase phase, When when, Reach reach, std::function<void()> run)
{
    if (phase == QcTickPhase::Messages || !run) {
        qCWarning(lcServer) << "Tick system refused: phase" << QcTickPhases::id(phase) << (run ? "is not part of the tick" : "has no entry point");
        return;
    }

    m_systems.push_back({phase, when, reach, std::move(run)});
}

void QcTickSystems::run(bool simulating, QcTickPhaseTimer &timer, const InEachDimension &inEachDimension) const
{
    for (const System &system : m_systems) {
        if (system.when == When::Simulating && !simulating) {
            continue;
        }

        if (system.reach == Reach::EachDimension && inEachDimension) {
            inEachDimension(system.run);
        } else {
            system.run();
        }
        timer.lap(system.phase);
    }
}
