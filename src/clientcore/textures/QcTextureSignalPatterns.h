#pragma once

#include "QcTexturePaint.h"

// Painter of a Signal Dust pattern (decision E-333): the wire's grains, the lever icon, the repeater's
// slab and the piston's side; nullptr for other patterns.
QcTexturePaint::Painter qcSignalPatternPainter(QcTextureSpec::Pattern pattern);
