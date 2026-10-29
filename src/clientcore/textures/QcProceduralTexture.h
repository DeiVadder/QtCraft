// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureSpec.h"
#include "common/content/blocks/QcTextureAnimation.h"
#include "common/core/QcResult.h"

#include <QImage>
#include <QJsonObject>
#include <QList>

// A procedural texture definition (content/*/textures/*.json or a texture pack's spec) turned into
// a picture: the fields are read into a QcTextureSpec and painted at 16 x 16; larger resolutions
// are enlarged with the pattern's detail (QcTextureDetail, decision E-299).
namespace QcProceduralTexture {

// The spec of a definition; the error names the field that is wrong.
[[nodiscard]] QcResult<QcTextureSpec> spec(const QJsonObject &definition);

// The texture at `resolution` (a power of two from 16 to 512); of an animated definition (decision
// E-878) frame `frame` (0 to frames - 1), painted at the phase frame / frames.
[[nodiscard]] QcResult<QImage> render(const QJsonObject &definition, int resolution, int frame = 0);

// Every frame of the definition's animation (one for a still texture, also when its animation
// fields are invalid: a server's content is checked by the content parser, not here).
[[nodiscard]] QcResult<QList<QImage>> renderFrames(const QJsonObject &definition, int resolution);

// The definition's animation; a still one when it has none or invalid fields.
[[nodiscard]] QcTextureAnimation animationOf(const QJsonObject &definition);

} // namespace QcProceduralTexture
