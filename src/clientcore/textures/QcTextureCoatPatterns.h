#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a coat pattern of creatures (decision E-880): large smooth-edged patches like a
// Holstein cow's; nullptr for the patterns other painters draw.
[[nodiscard]] QcTexturePaint::Painter qcCoatPatternPainter(QcTextureSpec::Pattern pattern);
