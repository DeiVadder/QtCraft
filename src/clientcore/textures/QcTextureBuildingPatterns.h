// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a building-block pattern (decision E-750): mossy, cracked and chiseled bricks, smooth
// stone, glazed terracotta, the door icon (decision E-831), ice without a frame and slime (decision
// E-860); nullptr for the patterns other painters draw.
[[nodiscard]] QcTexturePaint::Painter qcBuildingPatternPainter(QcTextureSpec::Pattern pattern);
