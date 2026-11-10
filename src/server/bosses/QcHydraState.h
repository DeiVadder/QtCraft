// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtGlobal>

#include <array>
#include <optional>

/*! \brief What a charred hydra (QcMobDefinition::Ai::Hydra, decisions E-1125, E-1126) keeps
    between ticks: the ticks of its charge left (\c {chargeTicks}, above 0 while it charges) and
    of its life (\c {ticks}), the targets of its main head and of its two side heads (entity ids of
    players or creatures, 0 for none), when each side head looks again and how often in a row it
    found nothing (\c {sideIdle}: then it fires at a random place), the ticks until the main head
    may fire again, the ticks until it breaks the blocks around it after a hurt, the heals of its
    charge and its regeneration it counts towards, whether the server is to set off its blast or
    break those blocks now (\c {blastDue}, \c {breakDue}), the height its feet keep while it has a
    target and where it wanders without one. Only \c {chargeTicks} is saved
    (QcMobManager::Stored::hydraCharge): a hydra loaded again goes on charging, the rest starts
    afresh. \qcfields */
struct QcHydraState
{
    int chargeTicks = 0;
    int ticks = 0;
    quint32 target = 0;
    std::array<quint32, 2> sideTargets{};
    std::array<int, 2> sideNext{};
    std::array<int, 2> sideIdle{};
    int attackTicks = 0;
    int breakTicks = 0;
    int healTicks = 0;
    int regenerationTicks = 0;
    bool blastDue = false;
    bool breakDue = false;
    std::optional<double> feetY;
    QcVec3 heading;
    int headingTicks = 0;

    /*! Ticks from a fresh hurt to the breaking of the blocks around it (the role model's 20). */
    static constexpr int breakDelayTicks = 20;
    /*! How much a hurt hurries the side heads' shots at random places (the role model's 3). */
    static constexpr int hurtHurry = 3;

    /*! Returns whether it still charges: it neither moves nor fights nor takes damage. */
    [[nodiscard]] bool charging() const { return chargeTicks > 0; }

    /*! A fresh hurt: the blocks around it break breakDelayTicks later (unless that already
        counts down), and the side heads fire sooner. */
    void noteHurt()
    {
        if (breakTicks <= 0) {
            breakTicks = breakDelayTicks;
        }
        for (int &idle : sideIdle) {
            idle += hurtHurry;
        }
    }
};
