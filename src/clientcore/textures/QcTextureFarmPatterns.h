// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

// Painter of a farming pattern (decision E-289): crop plants, reeds (decision E-850), stems, farmland, pumpkin and melon
// faces and the food icons; nullptr for other patterns (the hoe is a tool of QcTextureSynthesizer).
QcTexturePaint::Painter qcFarmPatternPainter(QcTextureSpec::Pattern pattern);
