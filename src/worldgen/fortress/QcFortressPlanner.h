// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/fortress/QcFortressNet.h"
#include "worldgen/fortress/QcFortressPlan.h"
#include "worldgen/nether/QcNetherDefinitions.h"

#include <memory>

class QcNetherComplexes;
class QcNetherTerrain;

/*! \brief Plans the fortress of a region (decision E-1071) from the seed and the terrain noise:
    whether the region has one, where its net stands, and for each room and passage whether rock
    or open air surrounds it, which makes it a gallery or a bridge.

    A pure function of the seed, the region and the content: it reads the noise only, never a
    chunk, and a fortress stays wholly inside its region, so a chunk needs the plans of just the
    regions it lies in. Thread-safe. */
class QcFortressPlanner
{
public:
    /*! Plans from \a definitions and \a terrain in the regions \a complexes gives a fortress, all
        of which outlive it; its places follow \a seed. */
    QcFortressPlanner(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes, quint64 seed);

    /*! Returns whether the settings ask for fortresses. */
    [[nodiscard]] bool enabled() const { return settings().regionSize > 0; }
    /*! Returns the settings. */
    [[nodiscard]] const QcNetherSettings::Fortress &settings() const { return m_definitions.settings.fortress; }
    /*! Returns the plan of the region \a regionX, \a regionZ; null without a fortress. */
    [[nodiscard]] std::shared_ptr<const QcFortressPlan> plan(int regionX, int regionZ) const;
    /*! Returns a plan with its first lattice point at \a corner (its lower floor at the y of
        it), where a demonstration world wants one; its shape follows rolls of its own, so it
        mirrors no natural fortress. */
    [[nodiscard]] std::shared_ptr<const QcFortressPlan> planAt(QcBlockPos corner) const;

private:
    // Where the rolls that shape a net come from: the region and a seed.
    struct Rolls
    {
        int regionX = 0;
        int regionZ = 0;
        quint64 seed = 0;
    };

    [[nodiscard]] quint64 seed() const { return m_seed; }
    // The share of the lattice points open at the height of a level on the floor of `corner`.
    [[nodiscard]] double openShare(QcBlockPos corner) const;
    // The share of the cells of the box in rock, sampled every `step` cells along x and z.
    [[nodiscard]] double rockShare(const QcBlockBox &box, int step) const;
    // The box of a part with the `shell` for the spawn area: a cell around it and down to the
    // rock under its `piers`.
    [[nodiscard]] QcBlockBox partBox(const QcBlockBox &shell, const QList<QcBlockPos> &piers) const;
    // The rooms of `net` with its first lattice point at `corner`, with the doors of `passages`.
    [[nodiscard]] QList<QcFortressPlan::Room> roomsOf(const QcFortressNet &net, QcBlockPos corner, const QList<QcFortressPlan::Passage> &passages) const;
    // The passages of the links of `net` with its first lattice point at `corner`.
    [[nodiscard]] QList<QcFortressPlan::Passage> passagesOf(const QcFortressNet &net, QcBlockPos corner) const;
    // The chests of `rooms`; `roll` decides which crossings hold one.
    [[nodiscard]] QList<QcFortressPlan::Chest> chestsOf(const QList<QcFortressPlan::Room> &rooms, const QcFortressNet::Roll &roll) const;
    [[nodiscard]] std::shared_ptr<const QcFortressPlan> build(QcBlockPos corner, Rolls rolls) const;

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
    const QcNetherComplexes &m_complexes;
    quint64 m_seed = 0;
};
