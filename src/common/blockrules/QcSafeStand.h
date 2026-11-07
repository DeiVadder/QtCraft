// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

class QcBlockAccess;

/*! \brief Where a player may appear without coming to harm (decision E-1111): the spawn search
    of the nether, the world spawn under a roof and the respawn beside a soul anchor.

    A cell fits when the block below the feet is solid, no liquid and does not hurt (no magma,
    no campfire), the feet and the head are free of solid blocks, liquids and anything that hurts
    or slows (fire, berry bushes, cobweb), and nothing that hurts or sets alight (lava, fire,
    magma) lies within hazardRadius around the feet, one layer below to one above the head.
    Unloaded cells count as not fitting. */
namespace QcSafeStand {

/*! Blocks around the feet that must be free of lava, fire and other hurting blocks. */
inline constexpr int hazardRadius = 2;

/*! Returns whether a player may stand with his feet in \a feet of \a world. */
[[nodiscard]] bool fits(const QcBlockAccess &world, QcBlockPos feet);

/*! Returns whether a player may stand with his feet in \a feet of \a world in the cells
    themselves, ignoring the hazards around (the respawn beside an anchor). */
[[nodiscard]] bool roomAt(const QcBlockAccess &world, QcBlockPos feet);

} // namespace QcSafeStand
