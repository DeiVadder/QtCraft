// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureSpec.h"
#include "common/content/blocks/QcTextureAnimation.h"
#include "common/core/QcResult.h"

#include <QImage>
#include <QJsonObject>
#include <QList>

/*! \brief A procedural texture definition (content/\unicode {0x2A}/textures/\unicode {0x2A}.json
    or a texture pack's spec) turned into a picture: the fields are read into a QcTextureSpec and
    painted at 16 x 16; larger resolutions are enlarged with the pattern's detail
    (QcTextureDetail, decision E-299). */
namespace QcProceduralTexture {

/*! Returns the spec of \a definition; the error names the field that is wrong. */
[[nodiscard]] QcResult<QcTextureSpec> spec(const QJsonObject &definition);

/*! Returns the texture of \a definition at \a resolution (a power of two from 16 to 512); of an
    animated definition (decision E-878) frame \a frame (0 to frames - 1), painted at the phase
    frame / frames. */
[[nodiscard]] QcResult<QImage> render(const QJsonObject &definition, int resolution, int frame = 0);

/*! Returns every frame of the animation of \a definition at \a resolution (one for a still
    texture, also when its animation fields are invalid: a server's content is checked by the
    content parser, not here). */
[[nodiscard]] QcResult<QList<QImage>> renderFrames(const QJsonObject &definition, int resolution);

/*! Returns the animation of \a definition; a still one when it has none or invalid fields. */
[[nodiscard]] QcTextureAnimation animationOf(const QJsonObject &definition);

} // namespace QcProceduralTexture
