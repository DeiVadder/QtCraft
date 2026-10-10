#pragma once

#include "QcAabb.h"
#include "QcBlockAccess.h"
#include "QcVec3.h"

#include <optional>

// Server check that a player can see what they act on (decision E-092): the same voxel raycast the
// client aims with (QcRaycast, visible blocks; liquids do not block) from the eye towards sample
// points of the target. Seeing any one of them is enough, so a target that only shows a corner
// still counts.
namespace QcLineOfSight {

// Whether a point of `box` (centre, corners and face centres, pulled 10 % towards the centre) is
// reached from `eye` without a visible block in between. Blocks of `target` itself do not count,
// nor does the block in the eye's own cell (unless that cell is the target).
bool reaches(const QcBlockAccess &world, const QcVec3 &eye, const QcAabb &box, std::optional<QcBlockPos> target = std::nullopt);

// The cell of a block as a box.
QcAabb blockBox(QcBlockPos pos);

} // namespace QcLineOfSight
