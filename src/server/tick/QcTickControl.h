// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <algorithm>

// Whether a tick simulates the world (decision E-249). A frozen world keeps ticking for the
// players (their movement, chunks, entity and time messages, saving) but runs none of the
// simulation: no time of day, block updates, liquids, creatures (spawning included), items,
// plants, falling blocks, machines or hunger. /tick step lets a number of ticks simulate while
// frozen. Not saved: a world always opens unfrozen.
class QcTickControl
{
public:
    // Most ticks one /tick step asks for (five minutes of game time).
    static constexpr quint64 maxSteps = 6000;

    bool frozen() const { return m_frozen; }
    void setFrozen(bool frozen)
    {
        m_frozen = frozen;
        m_steps = 0;
    }
    // Adds ticks to simulate while frozen.
    void step(quint64 ticks) { m_steps = std::min(m_steps + ticks, maxSteps); }
    quint64 pendingSteps() const { return m_steps; }

    // Whether the next tick simulates; takes one step while frozen.
    bool simulateNext()
    {
        if (!m_frozen) {
            return true;
        }
        if (m_steps == 0) {
            return false;
        }
        --m_steps;
        return true;
    }

private:
    bool m_frozen = false;
    quint64 m_steps = 0;
};
