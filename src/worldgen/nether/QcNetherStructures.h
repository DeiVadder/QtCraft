// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcXzRect.h"
#include "common/world/QcChunk.h"
#include "common/world/QcWorldLabel.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/nether/QcNetherDefinitions.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QList>
#include <QStringList>

#include <memory>
#include <optional>

class QcFortressGenerator;
class QcNetherTerrain;

/*! \brief The structures of the nether (decision E-1032): at most one copy of a kind per square
    region, wholly inside it, on a floor above the lava with room for it, and the arrival station
    of a demonstration world.

    The plans read only the terrain's noise, so they need no cache and no chunks, and a chunk asks
    just the regions it lies in. Thread-safe. */
class QcNetherStructures
{
public:
    /*! Plans from \a definitions and \a terrain, which outlive it, with the \a content of the
        placements (which must outlive it too); no copy reaches into a fortress of \a fortresses,
        which outlives it too. */
    QcNetherStructures(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, std::shared_ptr<const QcContentRegistry> content,
                       const QcFortressGenerator &fortresses);

    /*! Puts the arrival station with its middle at \a middle (standing on the floor at its y). */
    void setStation(QcBlockPos middle);
    /*! Returns the station's placement, if there is one. */
    [[nodiscard]] const std::optional<QcStructurePlacement> &station() const { return m_station; }

    /*! Returns the copy of \a kind in the region (\a regionX, \a regionZ), if it has one. */
    [[nodiscard]] std::optional<QcStructurePlacement> planOf(const QcNetherDefinitions::Structure &kind, int regionX, int regionZ) const;
    /*! Clears the room of the copies touching \a chunk, lays their foundations and writes them
        into it. */
    void build(QcChunk &chunk) const;
    /*! Returns the loot of the copies' chests in the chunk \a pos. */
    [[nodiscard]] QcChunkFurnishing furnishingIn(QcChunkPos pos) const;
    /*! Returns the floating labels of the copies in the chunk \a pos. */
    [[nodiscard]] QList<QcWorldLabel> labelsIn(QcChunkPos pos) const;
    /*! Returns the names /locate takes. */
    [[nodiscard]] QStringList names() const;
    /*! Returns the copy of the kind \a name nearest to \a from within about \a reach blocks. */
    [[nodiscard]] std::optional<QcStructurePlacement> nearest(const QString &name, QcBlockPos from, int reach) const;

private:
    // The copies of `kind` in the regions touching the block area `area`.
    [[nodiscard]] QList<QcStructurePlacement> plansIn(const QcNetherDefinitions::Structure &kind, QcXzRect area) const;
    // The plans within `margin` blocks of the chunk at `pos`.
    [[nodiscard]] QList<QcStructurePlacement> plansTouching(QcChunkPos pos, int margin = 0) const;
    // Turns pool cells around and over the footprint of `plan` in `chunk` into their rim, so that
    // what the footprint clears opens no pool (decision E-1047).
    void sealPools(QcChunk &chunk, const QcStructurePlacement &plan) const;
    // Empties the footprint of `plan` in `chunk` and fills the ground below it.
    void prepare(QcChunk &chunk, const QcStructurePlacement &plan) const;

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
    std::shared_ptr<const QcContentRegistry> m_content;
    const QcFortressGenerator &m_fortresses;
    std::optional<QcStructurePlacement> m_station;
};
