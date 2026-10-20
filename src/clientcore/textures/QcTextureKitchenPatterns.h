#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a kitchen pattern (decisions E-772 to E-774): the cake's top, side and icon, the
// campfire's icon and the lily pad; nullptr for the patterns other painters draw.
[[nodiscard]] QcTexturePaint::Painter qcKitchenPatternPainter(QcTextureSpec::Pattern pattern);
