// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillagePlan.h"

/*! \brief The road surfaces of a village (decision E-406): one surface per rank chosen from the
    style, and slabs where a road steps up by one block, so terraced ground reads as a stair.

    Pure functions of the plan, like the rest of it. */
namespace QcVillageRoads {

/*! Returns one of \a roads (not empty) by weight; \a random in [0, 1). */
const QcVillageStyle::Road &choose(const QList<QcVillageStyle::Road> &roads, double random);

/*! Returns the steps of the roads of \a plan: every road cell with a neighbouring road cell (of
    either rank) exactly one block higher gets its rank's step on top (\a step for the roads from
    the centre, \a branchStep for the side roads), unless that is air. Where the ranks share a
    column the road from the centre counts. Sorted by x, then z. */
[[nodiscard]] QList<QcVillagePlan::Step> steps(const QcVillagePlan &plan, QcBlockId step, QcBlockId branchStep);

} // namespace QcVillageRoads
