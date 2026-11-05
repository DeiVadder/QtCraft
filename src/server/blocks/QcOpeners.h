// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"

class QcServerBlocks;
class QcServerSounds;
class QcServerState;

/*! \brief Opens and closes doors, trapdoors and gates, for players and for circuits: both halves
    of a door together, with the block's sound, but never onto a living body (a player or
    creature inside the new shape keeps it as it is). */
class QcOpeners
{
public:
    QcOpeners(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    /*! Flips the \c {open} state of the opener \a block at \a pos. Returns \c false when the
        block at \a pos stays as it was (a body in the way, or the world refused the change), so
        a circuit tries again. */
    bool toggle(QcBlockPos pos, QcBlockId block);

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
};
