#include "clientcore/textures/QcTextureCraftPatterns.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

QColor paintNugget(const QcTextureSpec &spec, int x, int y, QColor)
{
    // Three small grains of different sizes.
    static constexpr std::array<std::array<int, 3>, 3> grains = {{{5, 6, 3}, {10, 9, 3}, {6, 11, 2}}};
    for (const auto &[cx, cy, radius] : grains) {
        const int dx = x - cx;
        const int dy = y - cy;
        if (dx * dx + dy * dy <= radius * radius) {
            return dx + dy < -radius / 2 ? varied(spec, spec.secondary, x, y, 3) : varied(spec, spec.primary, x, y);
        }
    }
    return clear();
}

QColor paintMetalBlock(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool outer = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    const bool light = (x == 1 || y == 1) && !outer;
    const bool dark = (x == size - 2 || y == size - 2) && !outer;
    const bool shine = x + y >= 6 && x + y <= 7 && x > 2 && y > 2;
    if (outer) {
        return scaled(spec.primary, 0.7);
    }
    if (light || shine) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    if (dark) {
        return scaled(spec.primary, 0.82);
    }
    return color;
}

QColor paintBars(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool bar = x % 5 == 1 || x % 5 == 2;
    const bool cross = y == 2 || y == 3 || y == size - 4 || y == size - 3;
    if (bar) {
        return x % 5 == 2 ? scaled(spec.primary, 0.75) : varied(spec, spec.primary, x, y);
    }
    if (cross) {
        return scaled(spec.primary, 0.85);
    }
    return clear();
}

QColor paintBookshelf(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool shelf = y <= 1 || y == 7 || y == 8 || y >= size - 2;
    if (shelf || x == 0 || x == size - 1) {
        return y == 8 || y == size - 1 ? scaled(spec.primary, 0.7) : color;
    }

    // Book spines in changing colours and heights; a gap here and there.
    const int row = y < 7 ? 0 : 1;
    const quint32 book = hashPixel(spec.seed + 81, x / 2, row);
    const int top = row == 0 ? 2 + int(book % 3) : 9 + int(book % 3);
    if (book % 7 == 0 || y < top) {
        return scaled(spec.primary, 0.45);
    }
    static constexpr std::array<QRgb, 5> spines = {0xff8a2a22, 0xff2a4a8a, 0xff3a6a2a, 0xff6a4a2a, 0xff7a6a3a};
    const QColor spine = QColor::fromRgba(spines[(book / 7) % spines.size()]);
    return x % 2 == 1 ? scaled(spine, 0.8) : varied(spec, spine, x, y, 5);
}

QColor paintPaper(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool sheet = x >= 3 && x <= 12 && y >= 2 && y <= 13;
    if (!sheet) {
        return clear();
    }
    if (x == 12 || y == 13) {
        return scaled(spec.primary, 0.85);
    }
    return y % 3 == 1 && x >= 5 && x <= 10 ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintBook(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool cover = x >= 3 && x <= 12 && y >= 2 && y <= 13;
    const bool pages = x == 12 && y >= 3 && y <= 12;
    if (pages) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    if (!cover) {
        return clear();
    }
    if (x == 3 || x == 4) {
        return scaled(spec.primary, 0.7);
    }
    return y == 5 && x >= 6 && x <= 10 ? scaled(spec.secondary, 0.9) : color;
}

// The bowl's shape; Stew additionally shows soup in the opening.
QColor paintBowlShape(const QcTextureSpec &spec, int x, int y, QColor color, bool filled)
{
    const double dx = (x - 7.5) / 6.5;
    const double dy = (y - 7.0) / 6.0;
    const bool body = dx * dx + dy * dy <= 1.0 && y >= 7 && y <= 12;
    const bool rim = y == 7 && x >= 1 && x <= 14;
    if (rim) {
        return filled ? varied(spec, spec.secondary, x, y, 3) : scaled(spec.primary, 0.6);
    }
    if (filled && y == 8 && x >= 3 && x <= 12) {
        return (hashPixel(spec.seed + 83, x, y) % 3) == 0 ? scaled(spec.secondary, 1.3) : varied(spec, spec.secondary, x, y, 3);
    }
    if (body) {
        return y == 12 || x == 2 || x == 13 ? scaled(spec.primary, 0.75) : color;
    }
    return clear();
}

QColor paintBowl(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintBowlShape(spec, x, y, color, false);
}

QColor paintStew(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintBowlShape(spec, x, y, color, true);
}

QColor paintApple(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 7.5;
    const double dy = y - 9.0;
    const bool fruit = dx * dx + dy * dy <= 30.0 && !(y == 4 && (x == 7 || x == 8));
    const bool stalk = x == 8 && y >= 1 && y <= 4;
    const bool leaf = y == 2 && x >= 9 && x <= 11;
    if (stalk) {
        return QColor(90, 60, 30);
    }
    if (leaf) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (!fruit) {
        return clear();
    }
    return dx < -1.5 && dy < -1.5 && dx > -4.0 ? scaled(spec.primary, 1.35) : color;
}

QColor paintLeather(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool hide = x >= 2 && x <= 13 && y >= 3 && y <= 12 && !((x == 2 || x == 13) && (y == 3 || y == 12));
    const bool stitch = (y == 4 || y == 11) && x >= 4 && x <= 11 && x % 2 == 0;
    if (!hide) {
        return clear();
    }
    return stitch ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintScrap(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // A ragged piece: the edge frays by noise.
    const double dx = x - 7.5;
    const double dy = (y - 8.0) * 1.4;
    if (dx * dx + dy * dy > 26.0 + 14.0 * noise(spec.seed + 85, x, y)) {
        return clear();
    }
    return (hashPixel(spec.seed + 87, x, y) % 5) == 0 ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintString(const QcTextureSpec &spec, int x, int y, QColor)
{
    // A loose loop of thread.
    const double dx = x - 7.5;
    const double dy = y - 7.0;
    const double radius = std::sqrt(dx * dx + dy * dy);
    const bool loop = std::abs(radius - 4.5) < 0.7;
    const bool tail = x + y == 22 && x >= 9 && x <= 13;
    return loop || tail ? varied(spec, spec.primary, x, y) : clear();
}

QColor paintCobweb(const QcTextureSpec &spec, int x, int y, QColor)
{
    const int dx = x - 7;
    const int dy = y - 7;
    const bool spoke = dx == 0 || dy == 0 || dx == dy || dx == -dy;
    const int ring = std::max(std::abs(dx), std::abs(dy));
    const bool thread = ring == 3 || ring == 6;
    if (spoke || thread) {
        return (hashPixel(spec.seed + 89, x, y) % 9) == 0 ? clear() : varied(spec, spec.primary, x, y);
    }
    return clear();
}

QColor paintDeadBush(const QcTextureSpec &spec, int x, int y, QColor)
{
    // A stem that forks twice; each twig runs diagonally upwards.
    const bool stem = (x == 7 || x == 8) && y >= 10;
    const bool left = x + y == 17 && x >= 3 && x <= 7 && y >= 6;
    const bool right = y - x == 3 && x >= 8 && x <= 12;
    const bool tips = (y - x == -1 && x >= 2 && x <= 4) || (x + y == 14 && x >= 11 && x <= 13);
    return stem || left || right || tips ? varied(spec, spec.primary, x, y) : clear();
}

QColor paintFern(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Three fronds from the base, each with leaflets on both sides.
    for (const int base : {4, 8, 12}) {
        const int lean = (base - 8) / 4;
        const int spine = base + lean * (size - 1 - y) / 4;
        const int reach = std::max(0, (y - 2) / 3);
        if (y >= 2 && std::abs(x - spine) <= reach && (x == spine || y % 2 == 0)) {
            return y < 5 ? varied(spec, spec.secondary, x, y, 3) : color;
        }
    }
    return clear();
}

QColor paintFireflyGlass(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool frame = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    if (frame) {
        return varied(spec, spec.primary, x, y, 5);
    }
    if ((hashPixel(spec.seed + 91, x, y) % 23) == 0) {
        return QColor(spec.secondary.red(), spec.secondary.green(), spec.secondary.blue(), 230);
    }
    return QColor(spec.primary.red(), spec.primary.green(), spec.primary.blue(), 60);
}

QColor paintBarrelSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool hoop = y == 2 || y == 3 || y == size - 4 || y == size - 3;
    if (hoop) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return x % 4 == 0 ? scaled(spec.primary, 0.75) : color;
}

QColor paintBarrelTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rim = x <= 1 || y <= 1 || x >= size - 2 || y >= size - 2;
    const bool handle = y >= 6 && y <= 9 && x >= 6 && x <= 9 && (x == 6 || x == 9 || y == 6 || y == 9);
    if (rim) {
        return scaled(spec.primary, 0.65);
    }
    if (handle) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return y % 4 == 0 ? scaled(spec.primary, 0.82) : color;
}

QColor paintStainedGlass(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool frame = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    const bool streak = (x - y == 3 || x - y == 4) && x > 2 && x < 10;
    if (frame) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (streak) {
        return QColor(255, 255, 255, 150);
    }
    return QColor(spec.primary.red(), spec.primary.green(), spec.primary.blue(), 120);
}

constexpr std::array<std::pair<Pattern, Painter>, 19> painters = {{
    {Pattern::Nugget, paintNugget},
    {Pattern::MetalBlock, paintMetalBlock},
    {Pattern::Bars, paintBars},
    {Pattern::Bookshelf, paintBookshelf},
    {Pattern::Paper, paintPaper},
    {Pattern::Book, paintBook},
    {Pattern::Bowl, paintBowl},
    {Pattern::Stew, paintStew},
    {Pattern::Apple, paintApple},
    {Pattern::Leather, paintLeather},
    {Pattern::Scrap, paintScrap},
    {Pattern::String, paintString},
    {Pattern::Cobweb, paintCobweb},
    {Pattern::DeadBush, paintDeadBush},
    {Pattern::Fern, paintFern},
    {Pattern::FireflyGlass, paintFireflyGlass},
    {Pattern::BarrelSide, paintBarrelSide},
    {Pattern::BarrelTop, paintBarrelTop},
    {Pattern::StainedGlass, paintStainedGlass},
}};

} // namespace

QcTexturePaint::Painter qcCraftPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}

bool qcCraftPatternIsTranslucent(QcTextureSpec::Pattern pattern)
{
    return pattern == Pattern::FireflyGlass || pattern == Pattern::StainedGlass;
}
