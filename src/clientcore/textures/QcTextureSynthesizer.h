// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureSpec.h"

#include <QImage>

// Generates deterministic pixel-art textures from a QcTextureSpec.
class QcTextureSynthesizer
{
public:
    static constexpr int tileSize = 16;

    static QImage synthesize(const QcTextureSpec &spec);
};
