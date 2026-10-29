// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a pattern of bows and arrows (decision E-425): the bow, the arrow and the feather
// icons; nullptr for other patterns.
QcTexturePaint::Painter qcRangedPatternPainter(QcTextureSpec::Pattern pattern);
