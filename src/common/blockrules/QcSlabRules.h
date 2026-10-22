#pragma once

#include "common/blockrules/QcBlockRules.h"

// Two slabs in one cell make a full block. A slab (state `type`: bottom, top, double) doubles up when
// it is clicked on its open half or placed into a slab of the other half. A vertical slab (decision
// E-755: `"facing": "against"` and the state `double`) stands against one side of its cell and
// doubles up when it is clicked on its open side or placed into a vertical slab facing the other way.
namespace QcSlabRules {

// The double slab that placing the slab `state` against `face` of `clicked` makes, std::nullopt when
// it makes none; `upper` is the half the click aimed at.
[[nodiscard]] std::optional<QcBlockRules::Placement> doubled(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked, QcBlockFace face,
                                                             bool upper);

// The same for the vertical slab `state` that would face `facing` where it is placed.
[[nodiscard]] std::optional<QcBlockRules::Placement> doubledVertical(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked,
                                                                     QcBlockFace face, const QString &facing);

// Whether the block is a vertical slab.
bool isVertical(const QcBlockDefinition &definition);

} // namespace QcSlabRules
