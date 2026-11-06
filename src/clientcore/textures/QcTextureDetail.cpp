// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureDetail.h"

#include "clientcore/textures/QcTextureOutline.h"
#include "clientcore/textures/QcTexturePaint.h"
#include "clientcore/textures/QcTextureRelief.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace QcTextureDetail {

namespace {

using Pattern = QcTextureSpec::Pattern;

// Colours closer than this (sum over red, green and blue) count as the same area when enlarging.
constexpr int similarColour = 48;
constexpr int similarAlpha = 32;
// A neighbour this much darker (luminance 0..1) marks an edge that gets a bevel.
constexpr double edgeStep = 0.12;

struct Pixel
{
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

Pixel pixelAt(const QImage &image, int x, int y)
{
    // Textures tile, so neighbours wrap around (an icon's border is transparent either way).
    const int wrappedX = (x + image.width()) % image.width();
    const int wrappedY = (y + image.height()) % image.height();
    const uchar *p = image.constScanLine(wrappedY) + static_cast<std::ptrdiff_t>(wrappedX) * 4;
    return {p[0], p[1], p[2], p[3]};
}

void setPixel(QImage &image, int x, int y, Pixel pixel)
{
    uchar *p = image.scanLine(y) + static_cast<std::ptrdiff_t>(x) * 4;
    p[0] = static_cast<uchar>(pixel.r);
    p[1] = static_cast<uchar>(pixel.g);
    p[2] = static_cast<uchar>(pixel.b);
    p[3] = static_cast<uchar>(pixel.a);
}

bool similar(Pixel first, Pixel second)
{
    if (first.a == 0 || second.a == 0) {
        return first.a == second.a;
    }
    const int colour = std::abs(first.r - second.r) + std::abs(first.g - second.g) + std::abs(first.b - second.b);
    return colour <= similarColour && std::abs(first.a - second.a) <= similarAlpha;
}

double luminance(Pixel pixel)
{
    return (0.299 * pixel.r + 0.587 * pixel.g + 0.114 * pixel.b) / 255.0;
}

// Whether two pixels lie on the same side of an outline: both transparent or both visible.
bool sameCoverage(Pixel first, Pixel second)
{
    return (first.a == 0) == (second.a == 0);
}

// The centre pixel of a Scale2x step and its four direct neighbours (above, left, right, below).
struct Neighbours
{
    Pixel e;
    Pixel b;
    Pixel d;
    Pixel f;
    Pixel h;
};

// Scale2x (EPX) with `belong` instead of "equal": each pixel becomes four, a corner takes the
// colour of the two neighbours meeting there when they belong together and the others do not.
template<typename Belong>
std::array<Pixel, 4> corners(const Neighbours &n, Belong belong)
{
    if (belong(n.b, n.h) || belong(n.d, n.f)) {
        return {n.e, n.e, n.e, n.e};
    }
    return {belong(n.d, n.b) ? n.d : n.e, belong(n.b, n.f) ? n.f : n.e, belong(n.d, n.h) ? n.d : n.e, belong(n.h, n.f) ? n.f : n.e};
}

// Pixel (x, y) of `source`: of a picture that repeats the pixel of the next tile, of an icon
// transparent beyond its border.
Pixel neighbourAt(const QImage &source, int x, int y, bool repeats)
{
    if (!repeats && !source.valid(x, y)) {
        return {};
    }
    return pixelAt(source, x, y);
}

// The outline (which quarters are visible) follows the coverage alone, so the pixel variation of
// a thin shape cannot notch, pinch or swell it; inside the shape, quarters take the colours of
// similar neighbours as before.
std::array<Pixel, 4> quarters(const QImage &source, int x, int y, bool repeats)
{
    const Neighbours n{pixelAt(source, x, y), neighbourAt(source, x, y - 1, repeats), neighbourAt(source, x - 1, y, repeats),
                       neighbourAt(source, x + 1, y, repeats), neighbourAt(source, x, y + 1, repeats)};
    const std::array<Pixel, 4> outline = corners(n, sameCoverage);
    const std::array<Pixel, 4> colour = corners(n, similar);
    std::array<Pixel, 4> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = sameCoverage(outline[i], colour[i]) ? colour[i] : outline[i];
    }
    return result;
}

// Enlarges `source` to twice its size; `repeats` tells whether it is a texture that tiles or an
// icon (only the smooth outline looks beyond the border).
QImage scale2x(const QImage &source, bool smooth, bool repeats)
{
    QImage target(source.width() * 2, source.height() * 2, QImage::Format_RGBA8888);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const Pixel e = pixelAt(source, x, y);
            const std::array<Pixel, 4> quarter = smooth ? quarters(source, x, y, repeats) : std::array<Pixel, 4>{e, e, e, e};
            setPixel(target, 2 * x, 2 * y, quarter[0]);
            setPixel(target, 2 * x + 1, 2 * y, quarter[1]);
            setPixel(target, 2 * x, 2 * y + 1, quarter[2]);
            setPixel(target, 2 * x + 1, 2 * y + 1, quarter[3]);
        }
    }
    return target;
}

int channel(double value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

Pixel shaded(Pixel pixel, double factor)
{
    pixel.r = channel(pixel.r * factor);
    pixel.g = channel(pixel.g * factor);
    pixel.b = channel(pixel.b * factor);
    return pixel;
}

// How a bevel is drawn: its strength (share of the colour), the pixels it fades over and whether
// only the outline (transparent neighbours) counts as an edge.
struct Bevel
{
    double strength = 0.0;
    int width = 1;
    bool rimOnly = false;
};

// Whether `other`, a pixel near `self`, lies across an edge: transparent next to a visible pixel,
// or (unless the bevel only follows the rim) much darker.
bool acrossEdge(Pixel self, Pixel other, Bevel bevel)
{
    if (other.a == 0) {
        return true;
    }
    return !bevel.rimOnly && luminance(other) < luminance(self) - edgeStep;
}

// Light from the top left: pixels near an edge above or to the left are lit, near an edge below or
// to the right shaded; the effect fades over the bevel's width.
double bevelFactor(const QImage &shape, int x, int y, Bevel bevel)
{
    const Pixel self = pixelAt(shape, x, y);
    if (self.a == 0) {
        return 1.0;
    }

    for (int distance = 1; distance <= bevel.width; ++distance) {
        const double weight = bevel.strength * (1.0 - static_cast<double>(distance - 1) / bevel.width);
        const bool lit = acrossEdge(self, pixelAt(shape, x, y - distance), bevel) || acrossEdge(self, pixelAt(shape, x - distance, y), bevel);
        const bool dark = acrossEdge(self, pixelAt(shape, x, y + distance), bevel) || acrossEdge(self, pixelAt(shape, x + distance, y), bevel);
        if (lit || dark) {
            return (lit ? 1.0 + weight : 1.0) * (dark ? 1.0 - weight : 1.0);
        }
    }
    return 1.0;
}

void addBevel(QImage &image, const QImage &shape, Bevel bevel)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const double factor = bevelFactor(shape, x, y, bevel);
            if (factor != 1.0) {
                setPixel(image, x, y, shaded(pixelAt(image, x, y), factor));
            }
        }
    }
}

// Two octaves of value noise at the pixel scale of the enlarged image.
void addGrain(QImage &image, double strength, quint32 seed)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const Pixel pixel = pixelAt(image, x, y);
            if (pixel.a == 0) {
                continue;
            }
            const double grain = 0.65 * QcTexturePaint::noise(seed, x, y) + 0.35 * QcTexturePaint::noise(seed + 1, x / 2, y / 2);
            setPixel(image, x, y, shaded(pixel, 1.0 + strength * grain));
        }
    }
}

bool among(Pattern pattern, std::initializer_list<Pattern> patterns)
{
    return std::ranges::find(patterns, pattern) != patterns.end();
}

} // namespace

Params forPattern(Pattern pattern)
{
    // Relief patterns are painted at the resolution with their own light: a little grain, no more.
    if (QcTextureRelief::isRelief(pattern)) {
        return {0.03, 0.0, false};
    }
    // Ground, stone and ore: grainy, round speckles, soft edges.
    if (among(pattern, {Pattern::Noise, Pattern::Speckle, Pattern::Ore, Pattern::Lump, Pattern::GrassSide, Pattern::Farmland, Pattern::Lava})) {
        return {0.08, 0.12, true};
    }
    if (pattern == Pattern::Water) {
        return {0.03, 0.0, true};
    }
    // Wood and built things of straight boards: crisp lines with a bevel.
    if (among(pattern, {Pattern::Planks, Pattern::LogSide, Pattern::LogTop, Pattern::Bookshelf, Pattern::BarrelSide, Pattern::BarrelTop,
                        Pattern::ChestSide, Pattern::ChestFront, Pattern::ChestTop, Pattern::DoorLower, Pattern::DoorUpper, Pattern::DoorItem,
                        Pattern::Trapdoor, Pattern::Ladder, Pattern::Torch, Pattern::Lantern})) {
        return {0.05, 0.18, false};
    }
    // Masonry and metal: strong bevels.
    if (among(pattern, {Pattern::Bricks, Pattern::Furnace, Pattern::MetalBlock, Pattern::Bars, Pattern::PumpkinSide,
                        Pattern::PumpkinTop, Pattern::MelonSide, Pattern::Repeater, Pattern::PistonSide})) {
        return {0.05, 0.22, false};
    }
    // Ice: no bevel (it would draw the edges of the streaks and cracks as ridges), a faint grain.
    if (among(pattern, {Pattern::Ice, Pattern::PackedIce})) {
        return {0.03, 0.0, true};
    }
    // Slime: a crisp rim without bevel or grain.
    if (pattern == Pattern::Slime) {
        return {0.0, 0.0, false};
    }
    if (among(pattern, {Pattern::Glass, Pattern::StainedGlass, Pattern::FireflyGlass})) {
        return {0.0, 0.1, false};
    }
    // Woven or soft surfaces of figures: a little grain, hardly any bevel.
    if (among(pattern, {Pattern::Limb, Pattern::Sleeve, Pattern::ArmourLeather, Pattern::Emblem, Pattern::Face, Pattern::Paper, Pattern::Patches,
                        Pattern::BackpackSide, Pattern::BackpackFront, Pattern::BackpackTop})) {
        return {0.04, 0.06, true};
    }
    // Tools and weapons are drawn of straight lines: their outline follows strokes, so a blade or
    // a handle stays straight and even at every resolution (Scale2x would leave steps that grow
    // into bumps with every doubling).
    if (among(pattern, {Pattern::Tool, Pattern::Pickaxe, Pattern::Axe, Pattern::Shovel, Pattern::Sword, Pattern::Hoe, Pattern::Stick,
                        Pattern::Arrow, Pattern::FishingRod})) {
        return {.grain = 0.03, .bevel = 0.22, .smooth = true, .rimOnly = true, .strokes = true};
    }
    // Everything else is a cut-out shape (plants, items, armour parts): smooth outlines, a bevel
    // only along the outline. Inside, the pixel variation of the 16 x 16 picture would count as
    // edges and draw ridges and seams across a blade or a tool's head.
    return {.grain = 0.03, .bevel = 0.22, .smooth = true, .rimOnly = true, .strokes = false};
}

Params withOverrides(Params params, const QJsonObject &detail)
{
    params.grain = std::clamp(detail.value(QLatin1StringView("grain")).toDouble(params.grain), 0.0, 1.0);
    params.bevel = std::clamp(detail.value(QLatin1StringView("bevel")).toDouble(params.bevel), 0.0, 1.0);
    params.smooth = detail.value(QLatin1StringView("smooth")).toBool(params.smooth);
    return params;
}

QImage enlarge(const QImage &base, int resolution, const Params &params, quint32 seed)
{
    QImage image = base.convertToFormat(QImage::Format_RGBA8888);
    // Tools and weapons (the patterns of strokes) are icons that never repeat; every other pattern
    // may tile.
    const bool repeats = !params.strokes;
    while (image.width() < resolution) {
        image = scale2x(image, params.smooth, repeats);
    }
    if (params.smooth && params.strokes) {
        image = QcTextureOutline::cut(image, base);
    }

    const QImage shape = image;
    if (params.bevel > 0.0) {
        addBevel(image, shape, {params.bevel, std::clamp(resolution / 32, 1, 4), params.rimOnly});
    }
    if (params.grain > 0.0) {
        addGrain(image, params.grain, seed * 31u + static_cast<quint32>(resolution));
    }
    return image;
}

} // namespace QcTextureDetail
