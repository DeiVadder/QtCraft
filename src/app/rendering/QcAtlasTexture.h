#pragma once

#include <QImage>
#include <QList>
#include <rhi/qrhi.h>

#include <functional>
#include <memory>
#include <optional>

// The texture atlas on the GPU with its mip levels (decision E-300), shared by the world and the
// character previews. Used from the render threads.
namespace QcAtlasTexture {

// The levels uploaded for an atlas: from the first level that fits `maxSize` (the GPU's largest
// texture; a smaller level when the atlas is too large) down to 1 x 1. The atlas brings its levels
// down to tiles of 8 pixels; the smaller ones are halved here (tiles of a few pixels, seen only far
// away, where neighbouring tiles may mix).
QList<QImage> chain(const QList<QImage> &levels, int maxSize);

// The first of `levels` within `maxSize` (or later) for which `creates` succeeds; unset when none does.
std::optional<qsizetype> usableLevel(const QList<QImage> &levels, int maxSize, const std::function<bool(QSize)> &creates);

// The magenta tile drawn when the GPU takes no atlas at all.
QImage placeholder();

// A mipmapped RGBA8 texture of `levels`, uploaded with `updates`. When the GPU refuses a level the
// next smaller one is tried, then the placeholder (logged); null only when that fails too.
std::unique_ptr<QRhiTexture> create(QRhi *rhi, const QList<QImage> &levels, QRhiResourceUpdateBatch *updates);

// Pixel art up close, mip levels blended with distance; clamped at the atlas border.
std::unique_ptr<QRhiSampler> createSampler(QRhi *rhi);

} // namespace QcAtlasTexture
