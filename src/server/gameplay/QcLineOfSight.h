#pragma once

#include "QcAabb.h"
#include "QcBlockAccess.h"
#include "QcVec3.h"

#include <optional>

struct QcServerClient;

// Server checks that a player can reach and see what they act on (decision E-092): the same voxel
// raycast the client aims with (QcRaycast, visible blocks; liquids do not block) from the eye towards sample
// points of the target. Seeing any one of them is enough, so a target that only shows a corner
// still counts.
namespace QcLineOfSight {

// Whether a point of `box` (centre, corners and face centres, pulled 10 % towards the centre) is
// reached from `eye` without a visible block in between. Blocks of `target` itself do not count,
// nor does the block in the eye's own cell (unless that cell is the target).
bool reaches(const QcBlockAccess &world, const QcVec3 &eye, const QcAabb &box, std::optional<QcBlockPos> target = std::nullopt);
// Whether `point` is reached from `eye` by one ray without a visible block in between (the block
// in the eye's own cell does not count): how a creature looks for a player (decision E-401).
bool seesPoint(const QcBlockAccess &world, const QcVec3 &eye, const QcVec3 &point);

// The cell of a block as a box.
QcAabb blockBox(QcBlockPos pos);

// Whether a block is within the player's reach from the eyes (QcRaycast::playerReach plus a metre
// of slack for movement between the client's decision and the server's check).
bool isWithinReach(const QcServerClient &player, QcBlockPos pos);
// Line of sight from the player's eyes, now or before the last input (decision E-092), to a block
// (which does not hide itself) or to a body.
bool canSee(const QcBlockAccess &world, const QcServerClient &player, QcBlockPos pos);
bool canSee(const QcBlockAccess &world, const QcServerClient &player, const QcAabb &body);

} // namespace QcLineOfSight
