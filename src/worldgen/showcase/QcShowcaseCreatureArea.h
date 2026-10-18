#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

// A glass enclosure for every creature of the registry, side by side (decision E-220): sized to the
// creature, roofed (with the podium's block for creatures that burn in daylight, decision E-426),
// with walls for hostile ones thicker than their reach (attackRange) beyond the glass, and a label.
// The creatures are spawned once with the world and stay (persistent and confined: one that
// teleports or takes blocks stays in its pen, decision E-460).
namespace QcShowcaseCreatureArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseCreatureArea
