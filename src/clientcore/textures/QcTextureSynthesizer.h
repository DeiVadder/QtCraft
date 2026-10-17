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
