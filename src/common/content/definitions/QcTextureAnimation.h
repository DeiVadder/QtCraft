// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QJsonObject>

// How a texture animates (decision E-878): `frames` pictures shown for `frameTicks` game ticks
// (50 ms) each, in a loop; with `interpolate` the picture blends smoothly into the next one. A
// texture definition declares it with "frames", "frameTime" (ticks) and "interpolate"; one frame
// is a still texture. The frames sit in the texture atlas and the block shader picks them by time
// (QcTextureAtlas::animationMap), so an animation costs no mesh rebuild and no upload per frame.
struct QcTextureAnimation
{
    // Limits of a definition: the atlas map stores frames and ticks in one byte each.
    static constexpr int maxFrames = 32;
    static constexpr int maxFrameTicks = 255;
    // Frame time of an animation whose source gives none (a texture pack's strip over a still
    // texture, a mod's PNG strip).
    static constexpr int defaultFrameTicks = 2;

    int frames = 1;
    int frameTicks = defaultFrameTicks;
    bool interpolate = false;

    bool animated() const { return frames > 1; }
    // Ticks of one loop.
    int cycleTicks() const { return frames * frameTicks; }

    // What is shown at a time: frame `index`, blended by `blend` (0..1) into frame `next`.
    struct Frame
    {
        int index = 0;
        int next = 0;
        double blend = 0.0;
    };
    // The frame at `ticks` since any start that is a multiple of cycleTicks(), the same as the
    // block shader picks (block.vert): deterministic, a still texture always shows frame 0.
    [[nodiscard]] Frame frameAt(double ticks) const;

    // The fields "frames" (1 to maxFrames), "frameTime" (1 to maxFrameTicks) and "interpolate" of
    // a texture definition; the error names the field that is wrong. Without "frames" a still
    // texture, "frameTime" and "interpolate" then are errors too.
    [[nodiscard]] static QcResult<QcTextureAnimation> read(const QJsonObject &definition);

    // Whether the procedural pattern `pattern` paints its frames differently (fire, lava, water;
    // QcTextureSynthesizer reads the phase only there): other patterns with "frames" show the same
    // picture in every frame, which the content loader warns about.
    [[nodiscard]] static bool patternMoves(const QString &pattern);

    bool operator==(const QcTextureAnimation &) const = default;
};
