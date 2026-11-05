// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcTickPhase.h"

#include <functional>
#include <vector>

class QcTickPhaseTimer;

/*! \brief The systems of the server tick as one list (decision E-700).

    A system is one entry point with the phase its time goes to (QcTickPhase, whose id names it
    in the phase timing, the debug display, the /tick and /worldinfo reports and the BENCH line)
    and the condition it runs under. The systems run in the order they were added; a phase may
    have several systems. The server builds the list once; it lives in the server's thread. */
class QcTickSystems
{
public:
    /*! When a system runs. */
    enum class When : quint8 {
        /*! In every tick, also in a frozen world (players, saving, chunks, the clients). */
        Always,
        /*! Only in ticks that simulate the world (not frozen, or a /tick step; decision E-249). */
        Simulating,
    };

    /*! \brief One system: the phase its time goes to, when it runs and what it runs. \qcfields */
    struct System
    {
        QcTickPhase phase = QcTickPhase::Players;
        When when = When::Always;
        std::function<void()> run;
    };

    /*! Adds the system \a run of \a phase, running \a when, after the others. Messages is no
        phase of the tick and is refused. */
    void add(QcTickPhase phase, When when, std::function<void()> run);
    /*! Runs one tick's systems in order (those of When::Simulating only when \a simulating);
        after each its phase gets a lap of \a timer. A system that laps another phase inside (the
        client sync laps the streaming per client) gives the rest to its own. */
    void run(bool simulating, QcTickPhaseTimer &timer) const;
    /*! Returns the systems in the order they run. */
    [[nodiscard]] const std::vector<System> &systems() const { return m_systems; }

private:
    std::vector<System> m_systems;
};
