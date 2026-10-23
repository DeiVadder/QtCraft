#pragma once

#include "common/world/QcChunk.h"
#include "common/world/QcWorldLabel.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcStructureSites.h"
#include "worldgen/generators/QcWorldSetup.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QStringList>

#include <memory>
#include <optional>

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
    // The guardian zones (decision E-591) and spawn areas (decision E-871) of the structures within
    // about `reach` blocks of `from` (whole regions); empty for generators without any.
    virtual QcStructureSites structureSitesNear(QcBlockPos from, int reach) const
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return {};
    }
    // The same from the plans made so far, never planning (the server's tick thread asks this);
    // std::nullopt when a region within `reach` is not planned yet (structureSitesNear plans it, on a
    // generation thread). Generators whose plans are all made up front answer structureSitesNear.
    [[nodiscard]] virtual std::optional<QcStructureSites> plannedStructureSitesNear(QcBlockPos from, int reach) const
    {
        return structureSitesNear(from, reach);
    }
    // The placed copy of the structure `name` that the generator places by itself (decision E-590),
    // alone or as a village piece (the temple at a desert village's centre, E-592), nearest to `from`
    // within about `reach` blocks (whole regions), if any.
    [[nodiscard]] virtual std::optional<QcStructurePlacement> nearestStructure(const QString &name, QcBlockPos from, int reach) const
    {
        Q_UNUSED(name);
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return std::nullopt;
    }
    // Labels floating in a chunk (decision E-222); the server sends them with the chunk.
    virtual QList<QcWorldLabel> labelsIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
    // What could not be resolved when the generator was made (unknown blocks or structures, replaced
    // by fallbacks or left out); its creator reports it.
    const QStringList &warnings() const { return m_warnings; }

protected:
    // Filled by the constructors of the generators.
    QStringList m_warnings;
};
