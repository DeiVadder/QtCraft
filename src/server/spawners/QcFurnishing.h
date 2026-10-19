#pragma once

#include "common/core/QcChunkPos.h"

class QcServerState;

// Puts the world generator's chest loot, spawner kinds and minecarts (decision E-570) into a chunk
// that arrives (decision E-520, QcWorldGenerator::furnishingIn), once per chunk: the chunk is
// marked furnished and the mark is saved with its block entities, so a chest emptied since stays
// empty and a cart taken away does not come back.
namespace QcFurnishing {

// Furnishes the loaded chunk at `pos` unless it is furnished already or has no spawner, no block with
// storage and no rail; a chunk the generator has nothing for is marked furnished too. Called for
// chunks whose blocks were just generated (the generator's plans for them are made; tick thread).
void furnish(QcServerState &state, QcChunkPos pos);

} // namespace QcFurnishing
