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
    // Where the ray meets the block's box, in world space.
    QcVec3 point;
};

// Voxel traversal (Amanatides & Woo) along a ray, tested against the boxes of each block state:
// the drawn boxes of every block that is neither air nor liquid nor invisible, or the collision
// boxes only (including the part of a fence above its own cell). Unloaded chunks stop the ray
// without a hit.
namespace QcRaycast {

enum class Targets : quint8 {
    // What the player can point at (also plants and torches).
    Visible,
    // What bodies collide with.
    Solid,
    // Visible blocks and liquids (an empty bucket takes a liquid).
    VisibleAndLiquids,
};

inline constexpr double playerReach = 5.0;

// Distance along a normalised ray to a box, or std::nullopt when it misses within maxDistance.
[[nodiscard]] std::optional<double> intersectBox(const QcVec3 &origin, const QcVec3 &direction, const QcAabb &box, double maxDistance);
// Like intersectBox, also telling the face through which the ray enters (the face it starts in for
// rays from inside the box: the one it leaves through is not useful, so it reports `insideFace`).
[[nodiscard]] std::optional<std::pair<double, QcBlockFace>> intersectBoxFace(const QcVec3 &origin, const QcVec3 &direction, const QcAabb &box,
                                                               double maxDistance, QcBlockFace insideFace);

[[nodiscard]] std::optional<QcRaycastHit> cast(const QcBlockAccess &world, const QcVec3 &origin, const QcVec3 &direction, double maxDistance,
                                 Targets targets = Targets::Visible);

} // namespace QcRaycast
