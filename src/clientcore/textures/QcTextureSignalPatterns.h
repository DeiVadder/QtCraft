#pragma once

#include "QcTexturePaint.h"

// Painter of a Signal Dust pattern (decision E-333): the wire's grains, the lever icon, the repeater's
// slab and the piston's side and the rest of the circuit parts, rails and the minecart (decision
// E-382); nullptr for other patterns.
QcTexturePaint::Painter qcSignalPatternPainter(QcTextureSpec::Pattern pattern);
