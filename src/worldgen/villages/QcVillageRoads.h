// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillagePlan.h"

// The road surfaces of a village (decision E-406): one surface per rank chosen from the style, and
// slabs where a road steps up by one block, so terraced ground reads as a stair. Pure functions of
// the plan, like the rest of it.
namespace QcVillageRoads {

// One of `roads` (not empty) by weight; `random` in [0, 1).
const QcVillageStyle::Road &choose(const QList<QcVillageStyle::Road> &roads, double random);

// The steps of the plan's roads: every road cell with a neighbouring road cell (of either rank)
// exactly one block higher gets its rank's step on top, unless that rank has none. Where the ranks
// share a column the road from the centre counts.
[[nodiscard]] QList<QcVillagePlan::Step> steps(const QcVillagePlan &plan, QcBlockId step, QcBlockId branchStep);

} // namespace QcVillageRoads
