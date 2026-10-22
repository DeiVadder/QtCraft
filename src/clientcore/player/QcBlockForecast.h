#pragma once

#include "common/blockrules/QcBlockRules.h"
#include "common/core/QcAabb.h"
#include "common/protocol/QcMessages.h"

#include <QList>

struct QcItemDefinition;

// The blocks the local player's actions are expected to set, as the server will set them (decision
// E-853): the client shows them at once and the server's acknowledgement confirms or rolls them back
// (QcBlockMirror). Only what the client can work out exactly is forecast; an empty list means the
// client waits for the server's blocks.
namespace QcBlockForecast {

// Breaking the block at `pos`: air there and, for a door or bed, at its other half (both in
// creative and survival mode, decision E-282). Empty where nothing (or air) is loaded.
[[nodiscard]] QList<QcMsg::BlockUpdate> breaking(const QcBlockAccess &world, QcBlockPos pos);

// Whether using `held` on a block does nothing but place its block (no bucket, food, stand, frame,
// cart, fire, dye or backpack with contents), so that the placement can be forecast.
[[nodiscard]] bool placesPlainly(const QcItemDefinition &held);

// Placing the block of `held` with `aim` from `eye` (QcBlockRules::placement): its cell and, for a
// door or bed, its other half. Empty when the item does not place plainly, the clicked block is a
// liquid or air, the block is a sapling or floats (their ground rules are the server's), the cell
// is out of reach or one of `bodies` (the living bodies nearby, the player's own too) would end up
// inside it.
[[nodiscard]] QList<QcMsg::BlockUpdate> placing(const QcBlockAccess &world, const QcItemDefinition &held, const QcBlockRules::Aim &aim,
                                                const QcVec3 &eye, const QList<QcAabb> &bodies);

} // namespace QcBlockForecast
