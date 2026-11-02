// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "common/content/blocks/QcTextureAnimation.h"
#include "common/content/blocks/QcTextureDefinition.h"
#include "common/core/QcResult.h"

#include <QImage>
#include <QList>
#include <QString>

#include <optional>

/*! \brief Where each texture of the atlas comes from (decision E-297): the active packs in priority
    order, then the content's own definition (the base 16 x 16 textures and those of mods).

    Within one pack a PNG image comes before a procedural spec of the same name, and a pack with
    "procedural": true finally renders the content's procedural definition at its resolution. */
namespace QcTextureSources {

/*! \brief A texture taken from the source that gives it, with its animation. */
struct Resolved
{
    /*! The texture, of an animation its first frame. */
    QImage image;
    /*! The id of the pack that gave the texture, "" for the content itself. */
    QString pack;
    /*! The animation (decision E-878) and its frames after the first, at the size of \c {image}.
        \qcshared */
    QcTextureAnimation animation = {};
    QList<QImage> laterFrames;

    /*! Returns all frames as one vertical strip, the first at the top (the form texture packs
        use). */
    [[nodiscard]] QImage strip() const;
};

/*! Returns the highest resolution of \a packs, at least 16 and at most \a maxResolution. */
int resolutionOf(const QcTexturePackStack &packs, int maxResolution);

/*! Returns the texture of \a definition at \a resolution, from \a packs or the content; the
    problem when even the content's own definition cannot be drawn. */
[[nodiscard]] QcResult<Resolved> resolve(const QcTextureDefinition &definition, const QcTexturePackStack &packs, int resolution);

/*! Returns the animation the texture of \a definition gets from the same source resolve() takes
    it from, without drawing it: the frames of a strip of one of \a packs (timed as the content's
    definition says, else QcTextureAnimation::defaultFrameTicks), a pack's or the content's
    procedural definition, the frames of a mod's PNG strip. */
[[nodiscard]] QcTextureAnimation animationOf(const QcTextureDefinition &definition, const QcTexturePackStack &packs);

/*! Returns \a image scaled to \a resolution x \a resolution: pixel-exact when enlarging,
    averaged when shrinking. */
QImage toResolution(const QImage &image, int resolution);

} // namespace QcTextureSources
