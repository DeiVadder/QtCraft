// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "common/world/QcGeneratorVersion.h"
#include "common/world/QcWorldLabel.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcLocatedPlace.h"
#include "worldgen/generators/QcStructureSites.h"
#include "worldgen/generators/QcWorldSetup.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QStringList>

#include <memory>
#include <optional>

struct QcVillagePlan;

/*! \brief Produces the unmodified terrain of a chunk from the world seed.

    Implementations must be deterministic and safe to call from several threads at once. */
class QcWorldGenerator
{
public:
    virtual ~QcWorldGenerator() = default;

    /*! Returns the generated chunk at \a pos. */
    virtual std::shared_ptr<QcChunk> generate(QcChunkPos pos) const = 0;
    /*! Returns the feet position for a new player near the world origin. */
    virtual QcBlockPos spawnPoint() const = 0;
    /*! Returns the name of the biome of the column \a x, \a z; empty for generators without
        biomes. */
    virtual QString biomeAt(int x, int z) const
    {
        Q_UNUSED(x);
        Q_UNUSED(z);
        return {};
    }
    /*! Returns whether a player may spawn standing on \a block: ground of the terrain itself,
        not a tree, plant or liquid. Generators without that knowledge accept every block. */
    virtual bool isNaturalGround(QcBlockId block) const
    {
        Q_UNUSED(block);
        return true;
    }
    /*! Returns the block of the generator's own spawn platform; the server puts one under a
        spawn without any ground (decision E-288). Without one it takes stone. */
    [[nodiscard]] virtual std::optional<QcBlockId> platformBlock() const { return std::nullopt; }
    /*! Returns what is set up once when a world is created (rules, creatures, contents, updates;
        decision E-221). */
    virtual QcWorldSetup setup() const { return {}; }
    /*! Returns the village of the region that holds the chunk \a pos (decision E-403); null
        without one or for generators without villages. */
    [[nodiscard]] virtual std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return nullptr;
    }
    /*! Returns the village whose centre lies nearest to \a from, at most about \a reach blocks
        away. */
    [[nodiscard]] virtual std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return nullptr;
    }
    /*! Returns the beds and workstations the generator places in the chunk \a pos outside every
        village plan that count as placed by a player (decision E-490): the server notes each one
        with the village state when the chunk enters the world (QcVillages::notePointOfInterest),
        so it joins a village near it or founds a settlement. The showcase's stations; empty for
        other generators. */
    virtual QList<QcBlockPos> pointsOfInterestIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
    /*! Returns the chest loot, spawner kinds and minecarts of the chunk \a pos (decision E-520),
        in world coordinates; the server puts them in when the chunk first arrives. Empty for
        generators without any. */
    virtual QcChunkFurnishing furnishingIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
    /*! Returns the guardian zones (decision E-591) and spawn areas (decision E-871) of the
        structures within about \a reach blocks of \a from (whole regions); empty for generators
        without any. */
    virtual QcStructureSites structureSitesNear(QcBlockPos from, int reach) const
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return {};
    }
    /*! Returns the same as structureSitesNear() for \a from and \a reach from the plans made so
        far, never planning (the server's tick thread
        asks this); std::nullopt when a region within \a reach is not planned yet
        (structureSitesNear plans it, on a generation thread). Generators whose plans are all
        made up front answer structureSitesNear. */
    [[nodiscard]] virtual std::optional<QcStructureSites> plannedStructureSitesNear(QcBlockPos from, int reach) const
    {
        return structureSitesNear(from, reach);
    }
    /*! Returns the placed copy of the structure \a name that the generator places by itself
        (decision E-590), alone or as a village piece (the temple at a desert village's centre,
        E-592), nearest to \a from within about \a reach blocks (whole regions), if any. */
    [[nodiscard]] virtual std::optional<QcStructurePlacement> nearestStructure(const QString &name, QcBlockPos from, int reach) const
    {
        Q_UNUSED(name);
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return std::nullopt;
    }
    /*! Returns the words /locate takes for this generator (decision E-911): "village", the kinds
        of places locate() finds ("mineshaft", "dungeon", "coastal_tower") and the structures it
        places by itself (nearestStructure(), the base game's without "qtcraft:"); empty for
        generators without any. */
    virtual QStringList locatableNames() const { return {}; }
    /*! Returns the place of kind \a kind ("mineshaft", "dungeon", "coastal_tower") nearest to
        \a from within about \a reach blocks, if any, and how far the search looked (it may stop
        short of \a reach within its budget of planned regions); no place for other words. */
    [[nodiscard]] virtual QcLocateResult locate(QStringView kind, QcBlockPos from, int reach) const
    {
        Q_UNUSED(kind);
        Q_UNUSED(from);
        return {std::nullopt, reach};
    }
    /*! Returns the labels floating in the chunk \a pos (decision E-222); the server sends them
        with the chunk. */
    virtual QList<QcWorldLabel> labelsIn(QcChunkPos pos) const
    {
        Q_UNUSED(pos);
        return {};
    }
    /*! Returns what could not be resolved when the generator was made (unknown blocks or
        structures, replaced by fallbacks or left out); its creator reports it. */
    const QStringList &warnings() const { return m_warnings; }
    /*! Returns the world generator version it generates as (decision E-922, QcGeneratorVersion);
        set by QcWorldGenerators::create, the current one for generators made directly. The
        server opens a world only with a generator of the world's version. */
    [[nodiscard]] int version() const { return m_version; }
    /*! Sets the world generator version it generates as to \a version. */
    void setVersion(int version) { m_version = version; }

protected:
    /*! Filled by the constructors of the generators. */
    QStringList m_warnings;

private:
    int m_version = QcGeneratorVersion::current;
};
