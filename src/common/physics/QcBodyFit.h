#pragma once

#include "QcPhysics.h"

#include <optional>

// Where a body fits into the world: clear of the collision boxes of the blocks around it.
namespace QcBodyFit {

// How cells without data (chunks not loaded) count.
enum class Unloaded { Clear, Blocked };

// Whether `box` overlaps a collision box of a block, including boxes reaching up from the cell
// below (fences, walls and gates are 1.5 m high). The box is shrunk by a millionth of a metre on
// every side, so a body resting against a face or a post after a clipped move does not count.
bool overlapsBlocks(const QcBlockAccess &world, const QcAabb &box, Unloaded unloaded);
bool overlapsBlocks(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize size, Unloaded unloaded);

// The feet position nearest to `feet` where the body is clear: `feet` itself when it is (cells
// not loaded count as clear there, so a body at the edge of the loaded world stays), else one
// metre up onto the top of the block it is in, a metre north, east, south or west, or a metre
// down, never below y 0 (cells not loaded block these). Nothing when all of them are blocked.
[[nodiscard]] std::optional<QcVec3> clearFeet(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize size);

} // namespace QcBodyFit
