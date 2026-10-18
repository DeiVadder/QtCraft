#pragma once

#include "common/core/QcChunkPos.h"

class QcServerState;

// Puts the world generator's chest loot and spawner kinds into a chunk that arrives (decision
// E-520, QcWorldGenerator::furnishingIn), once per chunk: the chunk is marked furnished and the mark
// is saved with its block entities, so a chest emptied since stays empty.
namespace QcFurnishing {

// Furnishes the loaded chunk at `pos` unless it is furnished already, has no spawner and no block
// with storage, or the generator has nothing for it.
void furnish(QcServerState &state, QcChunkPos pos);

} // namespace QcFurnishing
