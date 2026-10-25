#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QVector4D>

// Camera-relative rendering (decision E-883). Geometry is stored relative to an origin of its own
// (a chunk section's corner, the place an entity mesh was built around), in floats that stay small;
// each draw takes the origin's offset from the render anchor, a whole block near the camera,
// subtracted here in double, as one per-instance entry (whole numbers for sections: exact). The
// shaders add it to every vertex and subtract the camera's offset from the anchor (one uniform per
// frame), so the GPU sees positions relative to the camera, as exact far from the world's origin as
// near it, and the view matrix is the camera's rotation alone (QcCamera::viewRotation). The entries
// change only when the anchor moves (QcDrawOrigins).
namespace QcRenderOrigin {

// The world coordinates the shaders' hashes see (the corner shading of leaves) repeat after this
// many blocks: the section's corner modulo the period goes with the entry, exact in a float.
inline constexpr int hashPeriod = 1024;

// How far the camera may move from the render anchor along any axis before the anchor follows it.
// Small enough that a vertex near the camera, its section's offset from the anchor plus its place
// in the section, stays a float of a few hundred at most (error below 2e-5 blocks); large enough
// that the entries are rewritten only every 128 blocks of travel.
inline constexpr double anchorReach = 128.0;

// Whether `camera` has left the reach of `anchor` along any axis.
[[nodiscard]] bool outOfReach(const QcVec3 &anchor, const QcVec3 &camera);

// The entry of a draw: xyz the offset of `origin` from `anchor` (float, from the difference in
// double), w the packed corner of a section for the hashes (sectionCell(), 0 for other geometry).
[[nodiscard]] QVector4D entry(const QcVec3 &origin, const QcVec3 &anchor, float cell = 0.0f);

// A section's corner modulo hashPeriod, in sixteens of a block along each axis packed as
// x + 64 y + 4096 z (a whole number below 2^18, exact in a float); the block shader unpacks it.
[[nodiscard]] float sectionCell(QcBlockPos corner);

// A block corner as a QcVec3.
[[nodiscard]] constexpr QcVec3 toVec3(QcBlockPos pos)
{
    return {static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(pos.z)};
}

// The whole block containing `position`: an origin near it for geometry built around a point.
[[nodiscard]] QcVec3 blockNear(const QcVec3 &position);

} // namespace QcRenderOrigin
