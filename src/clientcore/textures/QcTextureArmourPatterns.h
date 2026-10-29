// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of an armour pattern (decision E-235): the item icons of helmet, chestplate, leggings
// and boots, and the textures armour layers and parts are drawn with on a body (plates, leather,
// a helmet front with an opening, a visor, trim); nullptr for other patterns.
QcTexturePaint::Painter qcArmourPatternPainter(QcTextureSpec::Pattern pattern);
