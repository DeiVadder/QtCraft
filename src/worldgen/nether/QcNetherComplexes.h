// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

class QcNetherTerrain;
struct QcNetherDefinitions;

/*! \brief Which of the great structures a region of the nether holds (decision E-1088): fortresses
    and bastions share one grid and one draw, so a region has at most one of them, as in the
    original game, where they are one set of structures with the weights 2 and 3. A bastion that
    falls on a biome it does not stand in gives way to a fortress.

    A pure function of the seed and the region; thread-safe. */
class QcNetherComplexes
{
public:
    /*! \brief What a region holds. */
    enum class Kind : quint8
    {
        /*! Neither. */
        None,
        /*! A fortress. */
        Fortress,
        /*! A bastion. */
        Bastion,
    };

    /*! Decides from \a definitions and \a terrain, which outlive it; the draws follow \a seed. */
    QcNetherComplexes(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, quint64 seed);

    /*! Returns what the region \a regionX, \a regionZ holds. */
    [[nodiscard]] Kind kindOf(int regionX, int regionZ) const;
    /*! Returns the middle of the bastion the region \a regionX, \a regionZ would hold, on its
        floor: anywhere it fits wholly inside the region. */
    [[nodiscard]] QcBlockPos bastionSite(int regionX, int regionZ) const;

private:
    [[nodiscard]] quint64 seed() const { return m_seed; }

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
    quint64 m_seed = 0;
};
