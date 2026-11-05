// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"

/*! Returns the painter of \a pattern, a pattern added with the storage blocks, crafted items and
    small plants (nuggets, bars, books, food, webs, raw metal, chains …); nullptr for the
    patterns QcTextureSynthesizer paints itself. */
QcTexturePaint::Painter qcCraftPatternPainter(QcTextureSpec::Pattern pattern);

/*! Returns whether \a pattern keeps partial transparency like glass. */
bool qcCraftPatternIsTranslucent(QcTextureSpec::Pattern pattern);
