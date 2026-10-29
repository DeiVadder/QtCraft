// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "common/content/definitions/QcTextureAnimation.h"
#include "common/content/definitions/QcTextureDefinition.h"
#include "common/core/QcResult.h"

#include <QImage>
#include <QList>
#include <QString>

#include <optional>

// Where each texture of the atlas comes from (decision E-297): the active packs in priority order,
// then the content's own definition (the base 16 x 16 textures and those of mods). Within one pack
// a PNG image comes before a procedural spec of the same name, and a pack with "procedural": true
// finally renders the content's procedural definition at its resolution.
namespace QcTextureSources {

struct Resolved
{
    // The texture, of an animation its first frame.
    QImage image;
    // The id of the pack that gave the texture, "" for the content itself.
    QString pack;
    // The animation (decision E-878) and its frames after the first, at the size of `image`.
    QcTextureAnimation animation = {};
    QList<QImage> laterFrames;

    // All frames as one vertical strip, the first at the top (the form texture packs use).
    [[nodiscard]] QImage strip() const;
};

// The highest resolution of the packs, at least 16 and at most `maxResolution`.
int resolutionOf(const QcTexturePackStack &packs, int maxResolution);

// The texture of `definition` at `resolution`; the problem when even the content's own definition
// cannot be drawn.
[[nodiscard]] QcResult<Resolved> resolve(const QcTextureDefinition &definition, const QcTexturePackStack &packs, int resolution);

// The animation the texture of `definition` gets from the same source resolve() takes it from,
// without drawing it: the frames of a pack's strip (timed as the content's definition says, else
// QcTextureAnimation::defaultFrameTicks), a pack's or the content's procedural definition, the
// frames of a mod's PNG strip.
[[nodiscard]] QcTextureAnimation animationOf(const QcTextureDefinition &definition, const QcTexturePackStack &packs);

// `image` scaled to `resolution` x `resolution`: pixel-exact when enlarging, averaged when shrinking.
QImage toResolution(const QImage &image, int resolution);

} // namespace QcTextureSources
