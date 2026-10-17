#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a farming pattern (decision E-289): crop plants, stems, farmland, pumpkin and melon
// faces and the food icons; nullptr for other patterns (the hoe is a tool of QcTextureSynthesizer).
QcTexturePaint::Painter qcFarmPatternPainter(QcTextureSpec::Pattern pattern);
