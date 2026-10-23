#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <algorithm>
#include <cmath>

// The zone a guardian keeps to (decision E-594): the guardian zone of a structure it came into
// (QcGuardSite, decision E-591), in world cells with both corners inside. Not saved: guardians are
// not persistent, their zone brings them back.
struct QcGuardPost
{
    quint64 site = 0;
    QcBlockPos min;
    QcBlockPos max;

    // The horizontal distance from a point to the zone's columns (0 inside) and the distance in
    // height to its cells, the larger of the two.
    double distanceTo(const QcVec3 &point) const
    {
        const double dx = std::max({min.x - point.x, 0.0, point.x - (max.x + 1.0)});
        const double dz = std::max({min.z - point.z, 0.0, point.z - (max.z + 1.0)});
        const double dy = std::max({min.y - point.y, 0.0, point.y - (max.y + 1.0)});
        return std::max(std::hypot(dx, dz), dy);
    }
    QcVec3 centre() const { return {(min.x + max.x + 1) / 2.0, static_cast<double>(min.y), (min.z + max.z + 1) / 2.0}; }
};
