#pragma once

#include "QcTexturePaint.h"

// Painter of an armour pattern (decision E-235): the item icons of helmet, chestplate, leggings
// and boots, and the textures armour layers and parts are drawn with on a body (plates, leather,
// a helmet front with an opening, a visor, trim); nullptr for other patterns.
QcTexturePaint::Painter qcArmourPatternPainter(QcTextureSpec::Pattern pattern);
