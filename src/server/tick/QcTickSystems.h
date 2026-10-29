// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcTickPhase.h"

#include <functional>
#include <vector>

class QcTickPhaseTimer;

// The systems of the server tick as one list (decision E-700). A system is one entry point with the
// phase its time goes to (QcTickPhase, whose id names it in the phase timing, the debug display, the
// /tick and /worldinfo reports and the BENCH line) and the condition it runs under. The systems run
// in the order they were added; a phase may have several systems. The server builds the list once;
// it lives in the server's thread.
class QcTickSystems
{
public:
    enum class When : quint8 {
        // In every tick, also in a frozen world (players, saving, chunks, the clients).
        Always,
        // Only in ticks that simulate the world (not frozen, or a /tick step; decision E-249).
        Simulating,
    };

    struct System
    {
        QcTickPhase phase = QcTickPhase::Players;
        When when = When::Always;
        std::function<void()> run;
    };

    // Adds a system after the others. Messages is no phase of the tick and is refused.
    void add(QcTickPhase phase, When when, std::function<void()> run);
    // Runs one tick's systems in order; after each its phase gets a lap of `timer`. A system that laps
    // another phase inside (the client sync laps the streaming per client) gives the rest to its own.
    void run(bool simulating, QcTickPhaseTimer &timer) const;
    [[nodiscard]] const std::vector<System> &systems() const { return m_systems; }

private:
    std::vector<System> m_systems;
};
