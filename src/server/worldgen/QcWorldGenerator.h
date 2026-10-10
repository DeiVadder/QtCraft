#pragma once

#include "QcChunk.h"

#include <memory>

// Produces the unmodified terrain of a chunk from the world seed. Implementations must be
// deterministic and safe to call from several threads at once.
class QcWorldGenerator
{
public:
    virtual ~QcWorldGenerator() = default;

    virtual std::shared_ptr<QcChunk> generate(QcChunkPos pos) const = 0;
    // Feet position for a new player near the world origin.
    virtual QcBlockPos spawnPoint() const = 0;
    // Name of the biome of a column; empty for generators without biomes.
    virtual QString biomeAt(int x, int z) const
    {
        Q_UNUSED(x);
        Q_UNUSED(z);
        return {};
    }
    // Whether a player may spawn standing on `block`: ground of the terrain itself, not a tree,
    // plant or liquid. Generators without that knowledge accept every block.
    virtual bool isNaturalGround(QcBlockId block) const
    {
        Q_UNUSED(block);
        return true;
    }
};
