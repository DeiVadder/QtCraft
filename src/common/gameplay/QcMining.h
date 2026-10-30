// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/items/QcItemDefinition.h"

/*! \brief Mining rules shared by server (validation) and client (progress display). */
namespace QcMining {

/*! Seconds per hardness point with the right tool at speed 1 (and by hand for tool-free blocks). */
inline constexpr double secondsPerHardness = 1.5;
/*! Seconds per hardness point when the block needs a tool the player does not use. */
inline constexpr double secondsPerHardnessWithoutTool = 5.0;
/*! Blocks with hardness 0 still take one tick, so a click cannot break a whole row at once. */
inline constexpr double minimumSeconds = 0.05;

/*! Returns whether \a block can be broken: a hardness that is not negative, no liquid, not
    invisible (air). */
bool isBreakable(const QcBlockDefinition &block);
/*! Returns \c true when the item \a held (nullptr = hand) gets the drops of \a block. */
bool canHarvest(const QcBlockDefinition &block, const QcItemDefinition *held);
/*! Returns the seconds needed to break \a block with the item \a held (nullptr = hand);
    negative for unbreakable blocks. \a speedBonus adds to a matching tool's speed (efficiency,
    QcEnchanting::speedBonus). */
double breakSeconds(const QcBlockDefinition &block, const QcItemDefinition *held, double speedBonus = 0.0);

} // namespace QcMining
