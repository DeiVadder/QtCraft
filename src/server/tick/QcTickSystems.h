// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcTickPhase.h"

#include <functional>
#include <utility>
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

    /*! What a system acts on (decision E-988). */
    enum class Reach : quint8 {
        /*! The whole server: it runs once. */
        Server,
        /*! What is in a dimension: it runs once per open dimension. */
        EachDimension,
    };

    /*! \brief One system: the phase its time goes to, when it runs, what it acts on and what it
        runs. \qcfields */
    struct System
    {
        QcTickPhase phase = QcTickPhase::Players;
        When when = When::Always;
        Reach reach = Reach::Server;
        std::function<void()> run;
    };

    /*! Runs a system once per open dimension, each time with that dimension as the current one. */
    using InEachDimension = std::function<void(const std::function<void()> &run)>;

    /*! Adds the system \a run of \a phase, running \a when, after the others, once for the whole
        server. Messages is no phase of the tick and is refused. */
    void add(QcTickPhase phase, When when, std::function<void()> run) { add(phase, when, Reach::Server, std::move(run)); }
    /*! Adds the system \a run of \a phase, running \a when and reaching \a reach, after the
        others. */
    void add(QcTickPhase phase, When when, Reach reach, std::function<void()> run);
    /*! Runs one tick's systems in order (those of When::Simulating only when \a simulating), those
        of Reach::EachDimension through \a inEachDimension (none: once); after each its phase
        gets a lap of \a timer. A system that laps another phase inside (the client sync laps the
        streaming per client) gives the rest to its own. */
    void run(bool simulating, QcTickPhaseTimer &timer, const InEachDimension &inEachDimension = {}) const;
    /*! Returns the systems in the order they run. */
    [[nodiscard]] const std::vector<System> &systems() const { return m_systems; }

private:
    std::vector<System> m_systems;
};
