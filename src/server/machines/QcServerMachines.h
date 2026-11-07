// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

class QcServerBlocks;
class QcServerSounds;
class QcServerState;

/*! \brief The machines (furnaces, brewing stands) in the server tick: they work by QcMachineRules
    and QcBrewingRules, tell comparators and hoppers when their slots changed, and crackle while
    they burn or bubble while they brew; a stand shows its bottles in its block state. */
class QcServerMachines
{
public:
    /*! A burning machine crackles on average this often (seconds). */
    static constexpr double machineSoundSeconds = 2.5;

    QcServerMachines(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    /*! Runs the machines for one server tick. */
    void tick();

private:
    void playMachineSounds();
    // The block state of every loaded stand follows what lies in its bottle slots.
    void showBottles();
    void playBrewedSound(QcBlockPos pos);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
};
