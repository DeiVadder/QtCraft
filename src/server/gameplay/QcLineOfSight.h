// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <optional>

struct QcServerClient;

/*! \brief Server checks that a player can reach and see what they act on (decision E-092): the same
    voxel raycast the client aims with (QcRaycast, visible blocks; liquids do not block) from the
    eye towards sample points of the target.

    Seeing any one of them is enough, so a target that only shows a corner still counts. */
namespace QcLineOfSight {

/*! Returns whether a point of \a box in \a world (centre, corners and face centres, pulled 10 %
    towards the centre) is reached from \a eye without a visible block in between. Blocks of
    \a target itself do not count, nor does the block in the eye's own cell (unless that cell is
    the target). */
bool reaches(const QcBlockAccess &world, const QcVec3 &eye, const QcAabb &box, std::optional<QcBlockPos> target = std::nullopt);
/*! Returns whether \a point is reached from \a eye in \a world by one ray without a visible
    block in between (the block in the eye's own cell does not count): how a creature looks for a
    player (decision E-401). */
bool seesPoint(const QcBlockAccess &world, const QcVec3 &eye, const QcVec3 &point);

/*! Returns the height of the eyes of \a player above his feet: QcPhysics::sneakEyeHeight while
    he crouches (sneaks on foot; in flight sneak descends, and the vehicles clear a rider's sneak),
    else QcPhysics::playerEyeHeight (decision E-961). */
[[nodiscard]] double eyeHeight(const QcServerClient &player);
/*! Returns where the eyes of \a player are now (eyeHeight()): rays and projectiles start there. */
[[nodiscard]] QcVec3 eyeOf(const QcServerClient &player);

/*! Returns the cell of the block at \a pos as a box. */
QcAabb blockBox(QcBlockPos pos);

/*! Returns whether the block at \a pos is within the reach of \a player from the eyes, standing
    or, while he sneaks or sneaked on the input before, crouching (decision E-961):
    QcRaycast::playerReach plus a metre of slack for movement between the client's decision and
    the server's check. */
bool isWithinReach(const QcServerClient &player, QcBlockPos pos);
/*! Returns whether \a player sees the block at \a pos of \a world from the eyes (standing, or
    crouching while he sneaks or sneaked on the input before, decision E-961), now or before the
    last input (decision E-092); the block does not hide itself. */
bool canSee(const QcBlockAccess &world, const QcServerClient &player, QcBlockPos pos);
/*! Returns whether \a player sees the \a body in \a world from the eyes (standing, or
    crouching while he sneaks or sneaked on the input before, decision E-961), now or before the
    last input (decision E-092). */
bool canSee(const QcBlockAccess &world, const QcServerClient &player, const QcAabb &body);

} // namespace QcLineOfSight
