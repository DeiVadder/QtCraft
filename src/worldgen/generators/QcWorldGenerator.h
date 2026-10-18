#pragma once

#include "common/world/QcChunk.h"
#include "common/world/QcWorldLabel.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcWorldSetup.h"

#include <memory>

struct QcVillagePlan;

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
    // Block of the generator's own spawn platform; the server puts one under a spawn without any
    // ground (decision E-288). Without one it takes stone.
    [[nodiscard]] virtual std::optional<QcBlockId> platformBlock() const { return std::nullopt; }
    // Set up once when a world is created (rules, creatures, contents, updates; decision E-221).
    virtual QcWorldSetup setup() const { return {}; }
    // The village of the region that holds a chunk (decision E-403); null without one or for
    // generators without villages.
    [[nodiscard]] virtual std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return nullptr;
    }
    // The village whose centre lies nearest to `from`, at most about `reach` blocks away.
    [[nodiscard]] virtual std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return nullptr;
    }
    // Beds and workstations the generator places in a chunk outside every village plan that count
    // as placed by a player (decision E-490): the server notes each one with the village state when
    // the chunk enters the world (QcVillages::notePointOfInterest), so it joins a village near it or
    // founds a settlement. The showcase's stations; empty for other generators.
    virtual QList<QcBlockPos> pointsOfInterestIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
    // The chest loot and spawner kinds of a chunk (decision E-520), in world coordinates inside it;
    // the server puts them in when the chunk first arrives. Empty for generators without any.
    virtual QcChunkFurnishing furnishingIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
    // Labels floating in a chunk (decision E-222); the server sends them with the chunk.
    virtual QList<QcWorldLabel> labelsIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
};
