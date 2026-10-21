#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a building-block pattern (decision E-750): mossy, cracked and chiseled bricks, smooth
// stone, glazed terracotta and the door icon (decision E-831); nullptr for the patterns other painters draw.
[[nodiscard]] QcTexturePaint::Painter qcBuildingPatternPainter(QcTextureSpec::Pattern pattern);
