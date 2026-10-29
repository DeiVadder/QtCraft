// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of an item frame or painting pattern (decision E-791): the frame's icon and the painting
// motifs, each drawn over its whole canvas of QcTextureSpec::tilesWide x tilesHigh blocks; nullptr
// for the patterns other painters draw.
[[nodiscard]] QcTexturePaint::Painter qcPaintingPatternPainter(QcTextureSpec::Pattern pattern);
