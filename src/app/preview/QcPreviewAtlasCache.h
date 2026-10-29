// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QList>
#include <rhi/qrhi.h>

#include <memory>

// The texture atlas of the character previews on the GPU, one texture per atlas image and QRhi
// shared by all previews (decision E-231): three previews (menu card, character screen, inventory)
// do not hold three copies, which matters for large texture packs. A texture lives as long as a
// preview renderer holds it. Used from the render threads.
namespace QcPreviewAtlasCache {

// The atlas (its mip levels, level 0 first) as a texture of `rhi`: the one a preview already
// uploaded, else a new one uploaded with `updates` (QcAtlasTexture). Null when the texture cannot
// be created.
std::shared_ptr<QRhiTexture> texture(QRhi *rhi, const QList<QImage> &levels, QRhiResourceUpdateBatch *updates);

// Atlas textures held by previews right now (log, test).
int liveTextures();

} // namespace QcPreviewAtlasCache
