#pragma once

#include "QcShowcaseArea.h"

// A glass enclosure for every creature of the registry, side by side (decision E-220): sized to the
// creature, roofed, with walls for hostile ones thicker than their reach (attackRange) beyond the
// glass, and a label.
// The creatures are spawned once with the world and stay (persistent).
namespace QcShowcaseCreatureArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseCreatureArea
