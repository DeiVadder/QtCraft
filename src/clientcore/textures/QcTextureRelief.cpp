// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureRelief.h"

#include "clientcore/textures/QcTextureFungalPatterns.h"
#include "clientcore/textures/QcTextureMasonryPatterns.h"
#include "clientcore/textures/QcTextureRockPatterns.h"
#include "clientcore/textures/QcTextureStonePatterns.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <span>

namespace QcTextureRelief {

namespace {

using QcTexturePaint::size;

// How strongly a slope of one pixel per pixel brightens or darkens, and how far the light goes.
constexpr double lightStrength = 0.3;
constexpr double brightest = 1.35;
constexpr double darkest = 0.6;

Painter painterOf(QcTextureSpec::Pattern pattern)
{
    for (const std::span<const Entry> table : {qcRockSurfaces(), qcStoneSurfaces(), qcMasonrySurfaces(), qcFungalSurfaces()}) {
        const auto it = std::ranges::find(table, pattern, &Entry::pattern);
        if (it != table.end()) {
            return it->painter;
        }
    }
    return nullptr;
}

// `point`, a little beyond the tile, on the tile: the surfaces repeat with it.
QPointF wrapped(QPointF point)
{
    return {std::fmod(point.x(), static_cast<double>(size)), std::fmod(point.y(), static_cast<double>(size))};
}

// The colour of pixel (x, y) of a texture with edges of `resolution` pixels.
// Where the slope at the tile's edge looks: around the tile, but a mat's edge ends at the foot of
// the tile, and what lies beyond it there is more of the same rock, not the mat at the top.
QPointF beyondPoint(const QcTextureSpec &spec, QPointF point)
{
    const QPointF around = wrapped(point);
    return spec.pattern == QcTextureSpec::Pattern::MatEdge ? QPointF(around.x(), std::min(point.y(), size - 0.001)) : around;
}

QColor colourAt(const QcTextureSpec &spec, Painter painter, QPoint pixel, int resolution)
{
    const double scale = static_cast<double>(size) / resolution;
    const QPointF point((pixel.x() + 0.5) * scale, (pixel.y() + 0.5) * scale);
    const Surface here = painter(spec, point);

    // The slope towards the bottom right; a slope falling that way faces the light of the top left.
    const double step = std::max(scale, 0.3);
    const Surface beyond = painter(spec, beyondPoint(spec, point + QPointF(step, step)));
    const double slope = (here.height - beyond.height) / step;
    const double light = std::clamp(1.0 + lightStrength * slope, darkest, brightest);
    const double grain = 0.5 * spec.variation * QcTexturePaint::noise(spec.seed + 3, pixel.x(), pixel.y());
    return QcTexturePaint::scaled(here.colour, light * (1.0 + grain));
}

} // namespace

bool isRelief(QcTextureSpec::Pattern pattern)
{
    return painterOf(pattern) != nullptr;
}

QImage paint(const QcTextureSpec &spec, int resolution)
{
    const Painter painter = painterOf(spec.pattern);
    QImage image(resolution, resolution, QImage::Format_RGBA8888);
    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            image.setPixelColor(x, y, colourAt(spec, painter, QPoint(x, y), resolution));
        }
    }
    return image;
}

Surface surfaceAt(const QcTextureSpec &spec, QPointF point)
{
    return painterOf(spec.pattern)(spec, point);
}

} // namespace QcTextureRelief
