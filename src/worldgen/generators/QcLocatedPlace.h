// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcIntMath.h"

#include <QList>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <tuple>

/*! \brief A place /locate finds that is not a placed structure (decision E-911): the central
    room of a mine shaft, the room of a dungeon, the door of a coastal tower; for a mine shaft
    also where its way up comes out on the ground. */
struct QcLocatedPlace
{
    /*! The position of the place. */
    QcBlockPos at;
    /*! Where a mine shaft's way up comes out on the ground; none for other places. */
    std::optional<QcBlockPos> entrance;

    bool operator==(const QcLocatedPlace &) const = default;
};

/*! \brief What a search for places gives (decision E-911): the place found, if any, and how far the
    search looked: every region within \c {searched} blocks was asked.

    A search that ran out of its budget of regions looked less far than it was asked to; a place
    it found beyond \c {searched} is the nearest of the regions it asked. */
struct QcLocateResult
{
    /*! The place found; none when the search found none. */
    std::optional<QcLocatedPlace> place;
    /*! How far the search looked in blocks. */
    int searched = 0;
};

/*! \brief Helpers for searching places region by region (decision E-911). */
namespace QcPlaceSearch {

/*! Returns the horizontal distance (x and z) between \a a and \a b. */
inline double distance(QcBlockPos a, QcBlockPos b)
{
    return std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.z - b.z));
}

/*! Returns the horizontal distance from \a from to the nearest column of region (\a regionX,
    \a regionZ), with regions of \a regionSize blocks. */
inline double regionDistance(QcBlockPos from, int regionX, int regionZ, int regionSize)
{
    const auto axis = [regionSize](int value, int region) {
        const int low = region * regionSize;
        return value < low ? low - value : std::max(0, value - (low + regionSize - 1));
    };
    return std::hypot(static_cast<double>(axis(from.x, regionX)), static_cast<double>(axis(from.z, regionZ)));
}

/*! Returns the place nearest to \a from within \a reach blocks of the places regions of
    \a regionSize blocks hold, at most one each and inside it (\a placeOf gives it as
    \c {placeOf(regionX, regionZ)}, std::optional<QcLocatedPlace>): regions are asked nearest
    first, and the search ends at the first region farther away than the best place so far, so
    planning a region (which may be costly, a mine shaft's) happens for a few regions only. At
    most \a maxRegions regions are asked: a miss would otherwise plan every region within reach
    on the caller's thread (the tick for /locate). */
template<typename PlaceOf>
[[nodiscard]] QcLocateResult nearestInRegions(QcBlockPos from, int reach, int regionSize, qsizetype maxRegions, const PlaceOf &placeOf)
{
    struct Region
    {
        double gap = 0.0;
        int x = 0;
        int z = 0;
    };
    QList<Region> regions;
    for (int rz = qcFloorDiv(from.z - reach, regionSize); rz <= qcFloorDiv(from.z + reach, regionSize); ++rz) {
        for (int rx = qcFloorDiv(from.x - reach, regionSize); rx <= qcFloorDiv(from.x + reach, regionSize); ++rx) {
            if (const double gap = regionDistance(from, rx, rz, regionSize); gap <= reach) {
                regions.append({gap, rx, rz});
            }
        }
    }
    // Ties in a fixed order, so the same place wins every time.
    std::ranges::sort(regions, [](const Region &a, const Region &b) { return std::tie(a.gap, a.z, a.x) < std::tie(b.gap, b.z, b.x); });

    std::optional<QcLocatedPlace> best;
    double bestDistance = static_cast<double>(reach);
    for (qsizetype index = 0; index < regions.size(); ++index) {
        const Region &region = regions.at(index);
        if (region.gap > bestDistance) {
            break;
        }
        if (index >= maxRegions) {
            // Out of budget: every place nearer than this region's nearest column was seen.
            return {best, static_cast<int>(std::floor(region.gap))};
        }
        const std::optional<QcLocatedPlace> place = placeOf(region.x, region.z);
        const double away = place ? distance(place->at, from) : 0.0;
        // The first of equally near places stays.
        if (place && (away < bestDistance || (!best && away <= bestDistance))) {
            bestDistance = away;
            best = place;
        }
    }
    return {best, reach};
}

} // namespace QcPlaceSearch
