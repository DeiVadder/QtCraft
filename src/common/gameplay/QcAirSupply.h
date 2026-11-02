// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtGlobal>

class QcBlockAccess;

/*! \brief The air of a player or a creature under water (decision E-955), counted in game ticks.

    With the eyes under water the air sinks by one each tick; at none the body drowns, one hit of
    drowningDamage every damageIntervalTicks for as long as it stays down. Out of the water the
    air comes back by refillPerTick each tick, as in the reference. The HUD shows it as
    bubbles. */
class QcAirSupply
{
public:
    /*! The air of a full breath: 15 s. */
    static constexpr int maxAir = 300;
    /*! Air that comes back in each tick with the eyes out of the water. */
    static constexpr int refillPerTick = 4;
    /*! Ticks between two hits of drowning once the air is gone. */
    static constexpr int damageIntervalTicks = 20;
    /*! Health one hit of drowning takes (one heart). */
    static constexpr double drowningDamage = 2.0;
    /*! Bubbles of a full breath in the HUD. */
    static constexpr int bubbles = 10;

    /*! Returns the air left, 0..maxAir. */
    int air() const { return m_air; }
    /*! Takes \a air from a save, clamped to 0..maxAir. */
    void restore(int air);
    /*! Fills the breath and forgets the drowning rhythm. */
    void reset();

    /*! Advances one tick with the eyes \a submerged or not. Returns whether a hit of drowning
        falls due in this tick. */
    [[nodiscard]] bool tick(bool submerged);

    /*! Returns the bubbles the HUD shows for \a air of a full breath of \a full: one for every
        started tenth of it, none only when the air is gone. */
    [[nodiscard]] static int bubblesOf(int air, int full = maxAir);
    /*! Returns whether eyes at \a eyes are under water in \a world: their cell holds water and
        they are below its surface (falling water fills its cell). */
    [[nodiscard]] static bool submergedAt(const QcBlockAccess &world, const QcVec3 &eyes);

private:
    // Sets the air left, already within 0..maxAir.
    void setAir(int air) { m_air = air; }
    // Ticks without air since the last hit of drowning.
    int drowningTicks() const { return m_drowningTicks; }
    void setDrowningTicks(int ticks) { m_drowningTicks = ticks; }

    int m_air = maxAir;
    int m_drowningTicks = 0;
};
