#include "QcTextureDetail.h"

#include "QcTexturePaint.h"

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
    const uchar *p = image.constScanLine(wrappedY) + std::ptrdiff_t(wrappedX) * 4;
    return {p[0], p[1], p[2], p[3]};
}

void setPixel(QImage &image, int x, int y, Pixel pixel)
{
    uchar *p = image.scanLine(y) + std::ptrdiff_t(x) * 4;
    p[0] = uchar(pixel.r);
    p[1] = uchar(pixel.g);
    p[2] = uchar(pixel.b);
    p[3] = uchar(pixel.a);
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

// Scale2x (EPX) with "similar" instead of "equal": each pixel becomes four, a corner takes the
// colour of the two neighbours meeting there when they belong together and the others do not.
std::array<Pixel, 4> quarters(const QImage &source, int x, int y)
{
    const Pixel e = pixelAt(source, x, y);
    const Pixel b = pixelAt(source, x, y - 1);
    const Pixel d = pixelAt(source, x - 1, y);
    const Pixel f = pixelAt(source, x + 1, y);
    const Pixel h = pixelAt(source, x, y + 1);
    if (similar(b, h) || similar(d, f)) {
        return {e, e, e, e};
    }
    return {similar(d, b) ? d : e, similar(b, f) ? f : e, similar(d, h) ? d : e, similar(h, f) ? f : e};
}

QImage scale2x(const QImage &source, bool smooth)
{
    QImage target(source.width() * 2, source.height() * 2, QImage::Format_RGBA8888);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const Pixel e = pixelAt(source, x, y);
            const std::array<Pixel, 4> quarter = smooth ? quarters(source, x, y) : std::array<Pixel, 4>{e, e, e, e};
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
    return std::clamp(int(std::lround(value)), 0, 255);
}

Pixel shaded(Pixel pixel, double factor)
{
    pixel.r = channel(pixel.r * factor);
    pixel.g = channel(pixel.g * factor);
    pixel.b = channel(pixel.b * factor);
    return pixel;
}

// Whether the pixel `distance` steps from (x, y) in direction (dx, dy) lies across an edge: much
// darker, or transparent next to a visible pixel.
bool acrossEdge(const QImage &shape, Pixel self, int x, int y, std::array<int, 3> step)
{
    const Pixel other = pixelAt(shape, x + step[0] * step[2], y + step[1] * step[2]);
    if (other.a == 0) {
        return true;
    }
    return luminance(other) < luminance(self) - edgeStep;
}

// Light from the top left: pixels near an edge above or to the left are lit, near an edge below or
// to the right shaded; the effect fades over `width` pixels.
double bevelFactor(const QImage &shape, int x, int y, double strength, int width)
{
    const Pixel self = pixelAt(shape, x, y);
    if (self.a == 0) {
        return 1.0;
    }

    for (int distance = 1; distance <= width; ++distance) {
        const double weight = strength * (1.0 - double(distance - 1) / width);
        const bool lit = acrossEdge(shape, self, x, y, {0, -1, distance}) || acrossEdge(shape, self, x, y, {-1, 0, distance});
        const bool dark = acrossEdge(shape, self, x, y, {0, 1, distance}) || acrossEdge(shape, self, x, y, {1, 0, distance});
        if (lit || dark) {
            return (lit ? 1.0 + weight : 1.0) * (dark ? 1.0 - weight : 1.0);
        }
    }
    return 1.0;
}

void addBevel(QImage &image, const QImage &shape, double strength, int width)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const double factor = bevelFactor(shape, x, y, strength, width);
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
    // Ground, stone and ore: grainy, round speckles, soft edges.
    if (among(pattern, {Pattern::Noise, Pattern::Speckle, Pattern::Ore, Pattern::Lump, Pattern::GrassSide, Pattern::Farmland, Pattern::Lava})) {
        return {0.08, 0.12, true};
    }
    if (pattern == Pattern::Water) {
        return {0.03, 0.0, true};
    }
    // Wood and built things of straight boards: crisp lines with a bevel.
    if (among(pattern, {Pattern::Planks, Pattern::LogSide, Pattern::LogTop, Pattern::Bookshelf, Pattern::BarrelSide, Pattern::BarrelTop,
                        Pattern::ChestSide, Pattern::ChestFront, Pattern::ChestTop, Pattern::DoorLower, Pattern::DoorUpper,
                        Pattern::Trapdoor, Pattern::Ladder, Pattern::Torch, Pattern::Lantern})) {
        return {0.05, 0.18, false};
    }
    // Masonry and metal: strong bevels.
    if (among(pattern, {Pattern::Bricks, Pattern::Furnace, Pattern::MetalBlock, Pattern::Bars, Pattern::PumpkinSide,
                        Pattern::PumpkinTop, Pattern::MelonSide})) {
        return {0.05, 0.22, false};
    }
    if (among(pattern, {Pattern::Glass, Pattern::StainedGlass, Pattern::FireflyGlass})) {
        return {0.0, 0.1, false};
    }
    // Woven or soft surfaces of figures: a little grain, hardly any bevel.
    if (among(pattern, {Pattern::Limb, Pattern::Sleeve, Pattern::ArmourLeather, Pattern::Emblem, Pattern::Face, Pattern::Paper})) {
        return {0.04, 0.06, true};
    }
    // Everything else is a cut-out shape (plants, items, armour parts): smooth outlines, rims.
    return {0.03, 0.22, true};
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
    while (image.width() < resolution) {
        image = scale2x(image, params.smooth);
    }

    const QImage shape = image;
    const int bevelWidth = std::clamp(resolution / 32, 1, 4);
    if (params.bevel > 0.0) {
        addBevel(image, shape, params.bevel, bevelWidth);
    }
    if (params.grain > 0.0) {
        addGrain(image, params.grain, seed * 31u + quint32(resolution));
    }
    return image;
}

} // namespace QcTextureDetail
