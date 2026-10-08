#pragma once

#include "QcVec3.h"

#include <algorithm>

// Axis-aligned bounding box in world space (metres).
struct QcAabb
{
    QcVec3 min;
    QcVec3 max;

    constexpr bool operator==(const QcAabb &) const = default;

    constexpr QcAabb moved(const QcVec3 &delta) const { return {min + delta, max + delta}; }
    constexpr QcAabb expandedTowards(const QcVec3 &delta) const
    {
        QcAabb box = *this;
        (delta.x < 0.0 ? box.min.x : box.max.x) += delta.x;
        (delta.y < 0.0 ? box.min.y : box.max.y) += delta.y;
        (delta.z < 0.0 ? box.min.z : box.max.z) += delta.z;
        return box;
    }
    constexpr bool intersects(const QcAabb &other) const
    {
        return min.x < other.max.x && max.x > other.min.x && min.y < other.max.y && max.y > other.min.y
               && min.z < other.max.z && max.z > other.min.z;
    }

    // Limits a movement along one axis so this box does not enter `obstacle`. Only boxes that overlap
    // on the two other axes can block; a box that already overlaps along the axis does not.
    constexpr double clipX(const QcAabb &obstacle, double delta) const
    {
        if (max.y <= obstacle.min.y || min.y >= obstacle.max.y || max.z <= obstacle.min.z || min.z >= obstacle.max.z) {
            return delta;
        }
        if (delta > 0.0 && max.x <= obstacle.min.x) {
            return std::min(delta, obstacle.min.x - max.x);
        }
        if (delta < 0.0 && min.x >= obstacle.max.x) {
            return std::max(delta, obstacle.max.x - min.x);
        }
        return delta;
    }
    constexpr double clipY(const QcAabb &obstacle, double delta) const
    {
        if (max.x <= obstacle.min.x || min.x >= obstacle.max.x || max.z <= obstacle.min.z || min.z >= obstacle.max.z) {
            return delta;
        }
        if (delta > 0.0 && max.y <= obstacle.min.y) {
            return std::min(delta, obstacle.min.y - max.y);
        }
        if (delta < 0.0 && min.y >= obstacle.max.y) {
            return std::max(delta, obstacle.max.y - min.y);
        }
        return delta;
    }
    constexpr double clipZ(const QcAabb &obstacle, double delta) const
    {
        if (max.x <= obstacle.min.x || min.x >= obstacle.max.x || max.y <= obstacle.min.y || min.y >= obstacle.max.y) {
            return delta;
        }
        if (delta > 0.0 && max.z <= obstacle.min.z) {
            return std::min(delta, obstacle.min.z - max.z);
        }
        if (delta < 0.0 && min.z >= obstacle.max.z) {
            return std::max(delta, obstacle.max.z - min.z);
        }
        return delta;
    }
};
