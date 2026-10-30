// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <algorithm>

/*! \brief Axis-aligned bounding box in world space (metres). */
struct QcAabb
{
    /*! The corner with the smallest coordinates. */
    QcVec3 min;
    /*! The corner with the largest coordinates. */
    QcVec3 max;

    constexpr bool operator==(const QcAabb &) const = default;

    /*! Returns the box shifted by \a delta. */
    constexpr QcAabb moved(const QcVec3 &delta) const { return {min + delta, max + delta}; }
    /*! Returns the box grown on each axis in the direction of \a delta by its length: the space
        it sweeps through when moved by \a delta. */
    constexpr QcAabb expandedTowards(const QcVec3 &delta) const
    {
        QcAabb box = *this;
        (delta.x < 0.0 ? box.min.x : box.max.x) += delta.x;
        (delta.y < 0.0 ? box.min.y : box.max.y) += delta.y;
        (delta.z < 0.0 ? box.min.z : box.max.z) += delta.z;
        return box;
    }
    /*! Returns whether the box overlaps \a other (touching faces do not count). */
    constexpr bool intersects(const QcAabb &other) const
    {
        return min.x < other.max.x && max.x > other.min.x && min.y < other.max.y && max.y > other.min.y
               && min.z < other.max.z && max.z > other.min.z;
    }

    /*! Overlaps up to this depth are rounding from earlier clipped moves: they count as
        touching. */
    static constexpr double touchTolerance = 1e-7;

    /*! Returns the movement \a delta along X limited so this box does not enter \a obstacle. Only
        boxes that overlap on the two other axes can block; a box that already overlaps along the
        axis does not. Within touchTolerance a box counts as touching: a body that ended a clipped
        move a rounding error inside a fence post is still stopped by it, and one that slides along
        a wall a rounding error inside it is not stopped by the seam to the next block. */
    constexpr double clipX(const QcAabb &obstacle, double delta) const
    {
        constexpr double t = touchTolerance;
        if (max.y <= obstacle.min.y + t || min.y >= obstacle.max.y - t || max.z <= obstacle.min.z + t
            || min.z >= obstacle.max.z - t) {
            return delta;
        }

        if (delta > 0.0 && max.x <= obstacle.min.x + t) {
            return std::min(delta, obstacle.min.x - max.x);
        }
        if (delta < 0.0 && min.x >= obstacle.max.x - t) {
            return std::max(delta, obstacle.max.x - min.x);
        }
        return delta;
    }
    /*! Returns the movement \a delta along Y limited so this box does not enter \a obstacle, as
        clipX() does along X. */
    constexpr double clipY(const QcAabb &obstacle, double delta) const
    {
        constexpr double t = touchTolerance;
        if (max.x <= obstacle.min.x + t || min.x >= obstacle.max.x - t || max.z <= obstacle.min.z + t
            || min.z >= obstacle.max.z - t) {
            return delta;
        }

        if (delta > 0.0 && max.y <= obstacle.min.y + t) {
            return std::min(delta, obstacle.min.y - max.y);
        }
        if (delta < 0.0 && min.y >= obstacle.max.y - t) {
            return std::max(delta, obstacle.max.y - min.y);
        }
        return delta;
    }
    /*! Returns the movement \a delta along Z limited so this box does not enter \a obstacle, as
        clipX() does along X. */
    constexpr double clipZ(const QcAabb &obstacle, double delta) const
    {
        constexpr double t = touchTolerance;
        if (max.x <= obstacle.min.x + t || min.x >= obstacle.max.x - t || max.y <= obstacle.min.y + t
            || min.y >= obstacle.max.y - t) {
            return delta;
        }

        if (delta > 0.0 && max.z <= obstacle.min.z + t) {
            return std::min(delta, obstacle.min.z - max.z);
        }
        if (delta < 0.0 && min.z >= obstacle.max.z - t) {
            return std::max(delta, obstacle.max.z - min.z);
        }
        return delta;
    }
};
