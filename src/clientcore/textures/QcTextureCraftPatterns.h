#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a pattern added with the storage blocks, crafted items and small plants (nuggets,
// bars, books, food, webs, raw metal, chains …); nullptr for the patterns QcTextureSynthesizer
// paints itself.
QcTexturePaint::Painter qcCraftPatternPainter(QcTextureSpec::Pattern pattern);

// Whether the pattern keeps partial transparency like glass.
bool qcCraftPatternIsTranslucent(QcTextureSpec::Pattern pattern);
