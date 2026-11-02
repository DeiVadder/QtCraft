// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureSpec.h"

#include <QImage>

/*! \brief Generates deterministic pixel-art textures from a QcTextureSpec. */
class QcTextureSynthesizer
{
public:
    /*! Edge of the synthesized textures in pixels. */
    static constexpr int tileSize = 16;

    /*! Returns the 16 x 16 texture of \a spec. */
    static QImage synthesize(const QcTextureSpec &spec);
};
