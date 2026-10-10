#pragma once

#include <QImage>
#include <rhi/qrhi.h>

#include <memory>

// The texture atlas of the character previews on the GPU, one texture per atlas image and QRhi
// shared by all previews (decision E-231): three previews (menu card, character screen, inventory)
// do not hold three copies, which matters for large texture packs. A texture lives as long as a
// preview renderer holds it. Used from the render threads.
namespace QcPreviewAtlasCache {

// The atlas `image` as a texture of `rhi`: the one a preview already uploaded, else a new one
// uploaded with `updates`. Null when the texture cannot be created.
std::shared_ptr<QRhiTexture> texture(QRhi *rhi, const QImage &image, QRhiResourceUpdateBatch *updates);

// Atlas textures held by previews right now (log, test).
int liveTextures();

} // namespace QcPreviewAtlasCache
