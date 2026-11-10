// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStrongholdDefinition.h"
#include "worldgen/strongholds/QcStrongholdPlan.h"

#include <functional>
#include <memory>

/*! \brief Plans a stronghold (decision E-1171) from the seed, its place and the height of the
    ground there: the pieces grown from the spiral staircase as in the original game, by weight
    and with limits per kind, each doorway leading on to a piece that fits or walled up; a plan
    without a portal room is grown anew with other rolls, and the last attempt gets one at its
    deepest walled-up doorway where it fits, so every plan has exactly one.

    A pure function of the seed, the stronghold's number and place and the ground's height,
    never of a chunk. Thread-safe. */
class QcStrongholdPlanner
{
public:
    /*! Returns the height of the ground's top block at the column \a x, \a z. */
    using Ground = std::function<int(int x, int z)>;

    /*! Plans with \a settings and \a ground, which outlive it; its rolls follow \a seed. */
    QcStrongholdPlanner(const QcStrongholdSettings &settings, Ground ground, quint64 seed);

    /*! Returns the settings. */
    [[nodiscard]] const QcStrongholdSettings &settings() const { return m_settings; }
    /*! Returns the plan of the stronghold \a index whose starting staircase stands on the column
        \a at (its y unused). */
    [[nodiscard]] std::shared_ptr<const QcStrongholdPlan> plan(int index, QcBlockPos at) const;
    /*! Returns the plan of a stronghold whose starting staircase has its lowest floor at \a at,
        with rolls of \a index (a demonstration world's). */
    [[nodiscard]] std::shared_ptr<const QcStrongholdPlan> planAt(int index, QcBlockPos at) const;

private:
    [[nodiscard]] quint64 seed() const { return m_seed; }

    const QcStrongholdSettings &m_settings;
    Ground m_ground;
    quint64 m_seed = 0;
};
