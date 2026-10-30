// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief How a texture animates (decision E-878): \c {frames} pictures shown for \c {frameTicks}
    game ticks (50 ms) each, in a loop; with \c {interpolate} the picture blends smoothly into the
    next one.

    A texture definition declares it with "frames", "frameTime" (ticks) and "interpolate"; one
    frame is a still texture. The frames sit in the texture atlas and the block shader picks them
    by time (QcTextureAtlas::animationMap), so an animation costs no mesh rebuild and no upload
    per frame. */
struct QcTextureAnimation
{
    /*! Most frames and most ticks per frame of a definition: the atlas map stores frames and
        ticks in one byte each. \qcshared */
    static constexpr int maxFrames = 32;
    static constexpr int maxFrameTicks = 255;
    /*! Frame time of an animation whose source gives none (a texture pack's strip over a still
        texture, a mod's PNG strip). */
    static constexpr int defaultFrameTicks = 2;

    /*! Number of frames (\c {"frames"}); 1 for a still texture. */
    int frames = 1;
    /*! Game ticks each frame is shown (\c {"frameTime"}). */
    int frameTicks = defaultFrameTicks;
    /*! Whether each frame blends into the next (\c {"interpolate"}). */
    bool interpolate = false;

    /*! Returns whether the texture has more than one frame. */
    bool animated() const { return frames > 1; }
    /*! Ticks of one loop. */
    int cycleTicks() const { return frames * frameTicks; }

    /*! \brief What is shown at a time: frame \c {index}, blended by \c {blend} (0..1) into frame
        \c {next}. */
    struct Frame
    {
        /*! Frame shown. */
        int index = 0;
        /*! Frame that follows \c {index} in the loop. */
        int next = 0;
        /*! Share of \c {next} in the blend, 0..1; always 0 without interpolation. */
        double blend = 0.0;
    };
    /*! Returns the frame at \a ticks since any start that is a multiple of cycleTicks(), the
        same as the block shader picks (block.vert): deterministic, a still texture always shows
        frame 0. */
    [[nodiscard]] Frame frameAt(double ticks) const;

    /*! Returns the fields "frames" (1 to maxFrames), "frameTime" (1 to maxFrameTicks) and
        "interpolate" of the texture definition \a definition; the error names the field that is
        wrong. Without "frames" a still texture, "frameTime" and "interpolate" then are errors
        too. */
    [[nodiscard]] static QcResult<QcTextureAnimation> read(const QJsonObject &definition);

    /*! Returns whether the procedural pattern \a pattern paints its frames differently (fire,
        lava, water; QcTextureSynthesizer reads the phase only there): other patterns with
        "frames" show the same picture in every frame, which the content loader warns about. */
    [[nodiscard]] static bool patternMoves(const QString &pattern);

    bool operator==(const QcTextureAnimation &) const = default;
};
