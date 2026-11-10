// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

class QcServerBlocks;
class QcServerExplosions;
class QcServerSounds;
class QcServerState;

/*! \brief The server's side of the charred hydra (decision E-1126; the role model's wither): what
    its brain asks for (QcHydraState::blastDue, QcHydraState::breakDue); part of QcServerCreatures.
    Summoning it is QcHydraSummoning.

    At the end of its charge it sets off a blast of QcHydraDefinition::blastPower at its middle
    head that spares its own kind and breaks blocks only under the game rule mobGriefing, and
    everyone in reach hears QcHydraDefinition::summonSound. A moment after a fresh hurt it breaks
    every block in a column breakColumn wide and breakHeight high from its feet, except air,
    liquids and unbreakable blocks (a portal breaks), with drops, and plays
    QcHydraDefinition::breakSound. */
class QcServerHydras
{
public:
    /*! The blocks it breaks after a hurt: breakColumn by breakColumn around its feet, breakHeight
        high (the role model's 3 by 3 by 4). \qcshared */
    static constexpr int breakColumn = 3;
    static constexpr int breakHeight = 4;

    QcServerHydras(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerExplosions &explosions);

    /*! Sets off the blasts and breaks the blocks the hydras of the current dimension asked for. */
    void tick();

private:
    struct Request;

    void blast(const Request &request);
    void breakAround(const Request &request);
    // Whether the hydra breaks the block at `pos`: not air, a liquid or an unbreakable block other
    // than a portal.
    bool breakable(QcBlockPos pos) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    QcServerExplosions &m_explosions;
};
