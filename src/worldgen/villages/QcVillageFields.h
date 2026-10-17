#pragma once

#include "worldgen/villages/QcVillageStyles.h"

#include <functional>
#include <memory>

// The fields of a village (decision E-406): the crop cells of a piece (blocks with `crop`) are
// planted from the style's crops, one crop per row (a run of crop cells along x in one layer), each
// cell ripe with ripeChance and otherwise at a random earlier stage. Draws come from the caller's
// sequence, so a field is a pure function of the seed like the rest of the plan.
namespace QcVillageFields {

inline constexpr double ripeChance = 0.5;

// Whether the structure has crop cells.
bool hasCrops(const QcStructure &structure, const QcContentRegistry &content);
// The crop cells of the structure (local cells).
QList<QcBlockPos> cropCells(const QcStructure &structure, const QcContentRegistry &content);

// A copy of `structure` with its crop cells planted from `crops` (not empty); `random` yields
// values in [0, 1).
[[nodiscard]] std::shared_ptr<const QcStructure> plant(const QcStructure &structure, const QList<QcVillageStyle::Crop> &crops,
                                                       const QcContentRegistry &content, const std::function<double()> &random);

} // namespace QcVillageFields
