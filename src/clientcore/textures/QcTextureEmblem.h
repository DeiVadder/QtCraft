#pragma once

#include "clientcore/textures/QcTexturePaint.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

#include <optional>

// The pattern "emblem" (decision E-241): pixel art from a small bitmap in the content data, on
// woven cloth in the primary colour. Cape emblems are drawn this way, so the motif is data and a
// mod can exchange it without code.
namespace QcTextureEmblem {

// Colour of the pixel (x, y): the palette's colour for the bitmap's character there, else cloth.
QColor paint(const QcTextureSpec &spec, int x, int y, QColor color);
// The pattern "icon": the palette's colour for the bitmap's character at (x, y), else transparent.
QColor paintIcon(const QcTextureSpec &spec, int x, int y, QColor color);

// `spec` with "bitmap" (an array of strings) and "palette" (character -> "#rrggbb") of a texture
// definition; the problem when they are malformed.
[[nodiscard]] QcResult<QcTextureSpec> read(const QJsonObject &json, QcTextureSpec spec);

} // namespace QcTextureEmblem
