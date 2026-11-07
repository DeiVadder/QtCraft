// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

class QcServerState;

/*! \brief Puts the world generator's chest loot, spawner kinds, minecarts (decision E-570) and
    residents (decision E-1089) into a chunk that arrives (decision E-520,
    QcWorldGenerator::furnishingIn), once per chunk: the chunk is marked furnished and the mark is
    saved with its block entities, so a chest emptied since stays empty and a cart taken away or a
    resident killed does not come back. */
namespace QcFurnishing {

/*! Furnishes the loaded chunk at \a pos of \a state unless it is furnished already or has no
    spawner, no block with storage, no rail and no resident; a chunk the generator has nothing for
    is marked furnished too. Called for chunks whose blocks were just generated (the generator's
    plans for them are made) and once for those a generator switch stored in its ring (tick
    thread). */
void furnish(QcServerState &state, QcChunkPos pos);
/*! Furnishes the chunk at \a pos of \a state whose blocks were generated once more: as furnish(),
    but the creatures it had stay as they are, since residents come once (decision E-1089). */
void furnishAgain(QcServerState &state, QcChunkPos pos);

} // namespace QcFurnishing
