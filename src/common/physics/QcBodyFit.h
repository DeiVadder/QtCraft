// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPhysics.h"

#include <optional>

/*! \brief Where a body fits into the world: clear of the collision boxes of the blocks around
    it. */
namespace QcBodyFit {

/*! \brief How cells without data (chunks not loaded) count. */
enum class Unloaded {
    /*! They count as free. */
    Clear,
    /*! They count as blocked. */
    Blocked
};

/*! Returns whether \a box overlaps a collision box of a block of \a world (cells without data as
    \a unloaded says), including boxes reaching up from the cell below (fences, walls and gates are
    1.5 m high). The box is shrunk by a millionth of a metre on every side, so a body resting
    against a face or a post after a clipped move does not count. */
bool overlapsBlocks(const QcBlockAccess &world, const QcAabb &box, Unloaded unloaded);
/*! Returns the same for the box of a body of \a size with its feet at \a feet. */
bool overlapsBlocks(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize size, Unloaded unloaded);

/*! Returns the feet position nearest to \a feet where a body of \a size is clear of the blocks of
    \a world: \a feet itself when it is (cells not loaded count as clear there, so a body at the
    edge of the loaded world stays), else one metre up onto the top of the block it is in, a metre
    north, east, south or west, or a metre down, never below y 0 (cells not loaded block these).
    Nothing when all of them are blocked. */
[[nodiscard]] std::optional<QcVec3> clearFeet(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize size);

} // namespace QcBodyFit
