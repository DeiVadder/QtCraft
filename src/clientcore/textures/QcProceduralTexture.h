#pragma once

#include "clientcore/textures/QcTextureSpec.h"
#include "common/core/QcResult.h"

#include <QImage>
#include <QJsonObject>

// A procedural texture definition (content/*/textures/*.json or a texture pack's spec) turned into
// a picture: the fields are read into a QcTextureSpec and painted at 16 x 16; larger resolutions
// are enlarged with the pattern's detail (QcTextureDetail, decision E-299).
namespace QcProceduralTexture {

// The spec of a definition; the error names the field that is wrong.
[[nodiscard]] QcResult<QcTextureSpec> spec(const QJsonObject &definition);

// The texture at `resolution` (a power of two from 16 to 512).
[[nodiscard]] QcResult<QImage> render(const QJsonObject &definition, int resolution);

} // namespace QcProceduralTexture
