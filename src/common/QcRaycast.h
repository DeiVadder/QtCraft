#pragma once

#include "QcBlockAccess.h"
#include "QcAabb.h"
#include "QcBlockFace.h"
#include "QcVec3.h"

#include <optional>

struct QcRaycastHit
{
    QcBlockPos block;
    // Face through which the ray entered the block; the block placed against it goes to its neighbour.
    QcBlockFace face = QcBlockFace::Up;
    double distance = 0.0;
};

// Voxel traversal (Amanatides & Woo) along a ray. Targets every block that is neither air nor
// liquid nor invisible; unloaded chunks stop the ray without a hit.
namespace QcRaycast {

inline constexpr double playerReach = 5.0;

// Distance along a normalised ray to a box, or std::nullopt when it misses within maxDistance.
std::optional<double> intersectBox(const QcVec3 &origin, const QcVec3 &direction, const QcAabb &box, double maxDistance);

std::optional<QcRaycastHit> cast(const QcBlockAccess &world, const QcVec3 &origin, const QcVec3 &direction, double maxDistance);

} // namespace QcRaycast
