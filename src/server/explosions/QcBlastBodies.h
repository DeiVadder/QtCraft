// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

class QcBlastField;
class QcPlayerHealth;
class QcServerSounds;
class QcServerState;

// What a blast does to players and creatures (decision E-602), like the reference game: a body
// within twice the power of the centre takes an impact of (1 - distance / (2 × power)) × exposure,
// the share of points of its box that see the centre past solid blocks (QcExplosionRays::exposure).
// The damage is (impact² + impact) / 2 × 7 × 2 × power + 1, armour takes its part for players; the
// push away from the centre grows with the impact (blastKnockback blows' worth at impact 1).
// Creative players and the dead are left alone; a body behind a wall of stone takes nothing.
class QcBlastBodies
{
public:
    // Blows' worth of push (QcMobManager::knockbackVelocity) at impact 1.
    static constexpr double blastKnockback = 3.0;

    struct Hits
    {
        int players = 0;
        int creatures = 0;
    };

    QcBlastBodies(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    Hits hurt(QcBlastField &field, const QcVec3 &centre, double power);

    // The impact on a body `distance` from the centre that sees it with `exposure` (0 beyond reach).
    static double impact(double distance, double power, double exposure);
    static double damageOf(double impact, double power);

private:
    int hurtPlayers(QcBlastField &field, const QcVec3 &centre, double power);
    int hurtCreatures(QcBlastField &field, const QcVec3 &centre, double power);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
};
