// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/bastion/QcBastionPlan.h"
#include "worldgen/nether/QcNetherDefinitions.h"

#include <memory>

class QcNetherComplexes;
class QcNetherTerrain;

/*! \brief Plans the bastion of a region (decision E-1087) from the seed and the terrain noise:
    whether the region has one, its form and which way it turns.

    A pure function of the seed, the region and the content: it reads the noise only, never a
    chunk, and a bastion stays wholly inside its region. Thread-safe. */
class QcBastionPlanner
{
public:
    /*! Plans from \a definitions and \a terrain in the regions \a complexes gives a bastion, all of
        which outlive it; its forms follow \a seed. */
    QcBastionPlanner(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes, quint64 seed);

    /*! Returns the settings. */
    [[nodiscard]] const QcNetherSettings::Bastion &settings() const { return m_definitions.settings.bastion; }
    /*! Returns whether the settings ask for bastions. */
    [[nodiscard]] bool enabled() const { return settings().regionSize > 0; }
    /*! Returns the plan of the region \a regionX, \a regionZ; null without a bastion. */
    [[nodiscard]] std::shared_ptr<const QcBastionPlan> plan(int regionX, int regionZ) const;
    /*! Returns a plan of \a form with its middle at \a centre, unturned, where a demonstration
        world wants one. */
    [[nodiscard]] std::shared_ptr<const QcBastionPlan> planAt(QcBastionPlan::Form form, QcBlockPos centre) const;

private:
    [[nodiscard]] quint64 seed() const { return m_seed; }
    [[nodiscard]] std::shared_ptr<QcBastionPlan> draw(QcBastionPlan plan, int turns) const;

    const QcNetherDefinitions &m_definitions;
    const QcNetherTerrain &m_terrain;
    const QcNetherComplexes &m_complexes;
    quint64 m_seed = 0;
};
