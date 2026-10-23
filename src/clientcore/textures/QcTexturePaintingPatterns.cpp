#include "clientcore/textures/QcTexturePaintingPatterns.h"

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

constexpr double tau = 2.0 * std::numbers::pi;

// Where a pixel of one tile lies on the painting's whole canvas: `u` to the right and `v` down in
// [0, 1], `px` and `py` in canvas pixels (16 per block). Without a tile the pixel samples the whole
// canvas shrunk into one block.
struct Canvas
{
    double u = 0.0;
    double v = 0.0;
    int px = 0;
    int py = 0;
    int width = size;
    int height = size;
};

Canvas canvasOf(const QcTextureSpec &spec, int x, int y)
{
    Canvas canvas;
    canvas.width = size * spec.tilesWide;
    canvas.height = size * spec.tilesHigh;
    if (spec.tile) {
        canvas.px = spec.tile->first * size + x;
        canvas.py = spec.tile->second * size + y;
    } else {
        canvas.px = x * spec.tilesWide + spec.tilesWide / 2;
        canvas.py = y * spec.tilesHigh + spec.tilesHigh / 2;
    }
    canvas.u = (canvas.px + 0.5) / canvas.width;
    canvas.v = (canvas.py + 0.5) / canvas.height;
    return canvas;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    const double s = std::clamp(t, 0.0, 1.0);
    const auto channel = [s](int from, int to) { return static_cast<int>(std::lround(from + (to - from) * s)); };
    return QColor(channel(a.red(), b.red()), channel(a.green(), b.green()), channel(a.blue(), b.blue()));
}

// A seeded value in [0, 1) for `salt`, the same for every pixel of a painting.
double seeded(const QcTextureSpec &spec, quint32 salt)
{
    return static_cast<double>(hashPixel(spec.seed, static_cast<int>(salt), 7) & 0xFFFFu) / 65536.0;
}

// The dark wooden rim around the whole canvas, or nothing inside it.
std::optional<QColor> rim(const Canvas &canvas)
{
    if (canvas.px == 0 || canvas.py == 0 || canvas.px == canvas.width - 1 || canvas.py == canvas.height - 1) {
        return QColor(88, 60, 36);
    }
    return std::nullopt;
}

// Brush texture: a little noise per canvas pixel, so flat fields look painted.
QColor brushed(const QcTextureSpec &spec, const QColor &color, const Canvas &canvas)
{
    return scaled(color, 1.0 + 0.5 * spec.variation * noise(spec.seed + 11, canvas.px, canvas.py));
}

QColor paintItemFrame(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Two pixels of wood with a darker inner edge around leather.
    const int edge = std::min({x, y, size - 1 - x, size - 1 - y});
    if (edge < 2) {
        return edge == 1 ? scaled(color, 0.8) : color;
    }
    return varied(spec, spec.secondary, x, y, 5);
}

QColor paintLandscape(const QcTextureSpec &spec, int x, int y, QColor)
{
    const Canvas c = canvasOf(spec, x, y);
    if (const std::optional<QColor> frame = rim(c)) {
        return *frame;
    }
    const double far = 0.55 + 0.07 * std::sin(c.u * tau * (1.0 + seeded(spec, 1) * 2.0) + seeded(spec, 2) * tau);
    const double near = 0.72 + 0.06 * std::sin(c.u * tau * (1.5 + seeded(spec, 3) * 2.0) + seeded(spec, 4) * tau);
    if (c.v > near) {
        return brushed(spec, scaled(spec.primary, 0.75), c);
    }
    if (c.v > far) {
        return brushed(spec, spec.primary, c);
    }
    const double sunU = 0.2 + 0.6 * seeded(spec, 5);
    const double aspect = static_cast<double>(c.width) / c.height;
    if (std::hypot((c.u - sunU) * aspect, c.v - 0.25) < 0.09) {
        return QColor(250, 222, 110);
    }
    return brushed(spec, mix(spec.secondary, QColor(235, 240, 250), c.v * 1.6), c);
}

QColor paintSea(const QcTextureSpec &spec, int x, int y, QColor)
{
    const Canvas c = canvasOf(spec, x, y);
    if (const std::optional<QColor> frame = rim(c)) {
        return *frame;
    }
    // A sail on its hull at the horizon.
    const double boat = 0.25 + 0.5 * seeded(spec, 1);
    const double rise = (0.5 - c.v) / 0.25;
    if (rise > 0.0 && rise < 1.0 && c.u > boat && c.u < boat + 0.1 * (1.0 - rise)) {
        return QColor(240, 236, 224);
    }
    if (c.v >= 0.5 && c.v < 0.54 && std::abs(c.u - boat - 0.04) < 0.08) {
        return QColor(96, 60, 36);
    }
    if (c.v < 0.5) {
        return brushed(spec, mix(spec.secondary, QColor(240, 244, 250), c.v * 2.0), c);
    }
    const bool crest = std::sin(c.v * 70.0 + std::sin(c.u * tau * 3.0 + seeded(spec, 2) * tau) * 2.0) > 0.85;
    return brushed(spec, crest ? scaled(spec.primary, 1.35) : mix(spec.primary, scaled(spec.primary, 0.6), (c.v - 0.5) * 2.0), c);
}

QColor paintAbstract(const QcTextureSpec &spec, int x, int y, QColor)
{
    const Canvas c = canvasOf(spec, x, y);
    if (const std::optional<QColor> frame = rim(c)) {
        return *frame;
    }
    // Fields between dark lines at seeded places; each field takes a colour of a small palette.
    const std::array<double, 3> columns = {0.2 + 0.2 * seeded(spec, 1), 0.45 + 0.2 * seeded(spec, 2), 0.75 + 0.15 * seeded(spec, 3)};
    const std::array<double, 2> rows = {0.25 + 0.25 * seeded(spec, 4), 0.6 + 0.25 * seeded(spec, 5)};
    const double lineU = 1.2 / c.width;
    const double lineV = 1.2 / c.height;
    int column = 0;
    for (const double at : columns) {
        if (std::abs(c.u - at) < lineU) {
            return QColor(30, 30, 34);
        }
        column += c.u > at ? 1 : 0;
    }
    int row = 0;
    for (const double at : rows) {
        if (std::abs(c.v - at) < lineV) {
            return QColor(30, 30, 34);
        }
        row += c.v > at ? 1 : 0;
    }
    const std::array<QColor, 5> palette = {spec.primary, spec.secondary, QColor(238, 234, 222), QColor(236, 196, 60), QColor(238, 234, 222)};
    return brushed(spec, palette[hashPixel(spec.seed, column, row) % palette.size()], c);
}

QColor paintNight(const QcTextureSpec &spec, int x, int y, QColor)
{
    const Canvas c = canvasOf(spec, x, y);
    if (const std::optional<QColor> frame = rim(c)) {
        return *frame;
    }
    const double peak = 0.6 + 0.25 * std::abs(std::fmod(c.u * (2.0 + 2.0 * seeded(spec, 1)) + seeded(spec, 2), 1.0) - 0.5) * 2.0;
    if (c.v > peak) {
        return brushed(spec, QColor(26, 28, 36), c);
    }
    const double aspect = static_cast<double>(c.width) / c.height;
    const double moonU = 0.15 + 0.7 * seeded(spec, 3);
    const double moon = std::hypot((c.u - moonU) * aspect, c.v - 0.22);
    if (moon < 0.08 && std::hypot((c.u - moonU - 0.03) * aspect, c.v - 0.2) > 0.07) {
        return spec.secondary;
    }
    if (hashPixel(spec.seed + 3, c.px, c.py) % 61 == 0) {
        return QColor(250, 250, 236);
    }
    return brushed(spec, mix(scaled(spec.primary, 0.6), spec.primary, c.v * 1.5), c);
}

QColor paintSunset(const QcTextureSpec &spec, int x, int y, QColor)
{
    const Canvas c = canvasOf(spec, x, y);
    if (const std::optional<QColor> frame = rim(c)) {
        return *frame;
    }
    constexpr double horizon = 0.6;
    const double aspect = static_cast<double>(c.width) / c.height;
    const double sunU = 0.3 + 0.4 * seeded(spec, 1);
    if (c.v < horizon && std::hypot((c.u - sunU) * aspect, c.v - horizon) < 0.16) {
        return QColor(255, 214, 96);
    }
    if (c.v < horizon) {
        return brushed(spec, mix(spec.secondary, spec.primary, c.v / horizon), c);
    }
    // The water mirrors the sky in broken stripes, the sun's path brightest.
    const bool glint = std::abs(c.u - sunU) < 0.12 * (1.0 - (c.v - horizon)) && std::sin(c.v * 90.0) > 0.0;
    const QColor water = mix(spec.primary, spec.secondary, (c.v - horizon) / (1.0 - horizon));
    return brushed(spec, glint ? QColor(255, 220, 120) : scaled(water, 0.7), c);
}

QColor paintRings(const QcTextureSpec &spec, int x, int y, QColor)
{
    const Canvas c = canvasOf(spec, x, y);
    if (const std::optional<QColor> frame = rim(c)) {
        return *frame;
    }
    const double aspect = static_cast<double>(c.width) / c.height;
    double wave = 0.0;
    for (quint32 centre = 0; centre < 3; ++centre) {
        const double cu = seeded(spec, 10 + centre);
        const double cv = seeded(spec, 20 + centre);
        wave += std::sin(std::hypot((c.u - cu) * aspect, c.v - cv) * 40.0);
    }
    return brushed(spec, wave > 0.6 ? spec.primary : wave < -1.2 ? scaled(spec.primary, 0.6) : spec.secondary, c);
}

constexpr std::array<std::pair<Pattern, Painter>, 7> painters = {{
    {Pattern::ItemFrame, paintItemFrame},
    {Pattern::PaintingLandscape, paintLandscape},
    {Pattern::PaintingSea, paintSea},
    {Pattern::PaintingAbstract, paintAbstract},
    {Pattern::PaintingNight, paintNight},
    {Pattern::PaintingSunset, paintSunset},
    {Pattern::PaintingRings, paintRings},
}};

} // namespace

QcTexturePaint::Painter qcPaintingPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
