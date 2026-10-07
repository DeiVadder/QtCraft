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
};
