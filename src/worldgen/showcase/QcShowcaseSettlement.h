// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

#include <optional>

/*! \brief The village station of the showcase (decision E-490), east of the rows of the village
    area: a bed growthGap blocks beyond the first row, which that row's village grows to hold, and
    gap blocks beyond the rows a settlement within one chunk: three beds and a composter, a ripe
    wheat field within reach of the composter, a chest of bread to throw, two homeless villagers
    that move in, and a short rail with a minecart to carry one away.

    The beds and the composter are points of interest of the area
    (QcShowcaseArea::pointsOfInterest): they count as placed, not as part of a village plan. */
namespace QcShowcaseSettlement {

/*! Blocks between the rows' east edge and the settlement (beyond joining any row). */
inline constexpr int gap = 80;
/*! Blocks between the first row's east edge and the bed that makes it grow. */
inline constexpr int growthGap = 20;

/*! \brief Where the station lies in the village area (local coordinates). */
struct Frame
{
    /*! The width of the rows. */
    int rowsWidth = 0;
    /*! The local z of the first (southernmost) row's road. */
    int firstRoad = 0;
    /*! The x, z of the settlement's north west corner (chunk aligned in world coordinates).
        \qcshared */
    int x = 0;
    int z = 0;
};

/*! Returns the local x of the settlement for rows \a rowsWidth wide in an area whose west edge
    is at world x \a west: chunk aligned. */
int settlementX(int rowsWidth, int west);
/*! Returns the width the area needs to hold the settlement at local x \a x (with a gap beyond
    it). */
int areaWidth(int x);
/*! Returns the local z of the settlement in an area \a depth deep whose north edge is at world z
    \a north: chunk aligned, and the settlement ends at least \a frontGap blocks before the area's
    south edge (beyond the reach of the areas south of it); none when the area is too shallow. */
std::optional<int> settlementZ(int depth, int north, int frontGap);

/*! Builds the station into \a area (whose north west corner on the ground is \a origin) with the
    blocks of \a context, where \a frame says; returns its points of interest in world
    coordinates. */
QList<QcBlockPos> build(const QcShowcaseContext &context, QcStructure &area, QcBlockPos origin, Frame frame);

} // namespace QcShowcaseSettlement
