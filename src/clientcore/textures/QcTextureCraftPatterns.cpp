// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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
    const int top = row == 0 ? 2 + static_cast<int>(book % 3) : 9 + static_cast<int>(book % 3);
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

// A chunk of raw metal: three overlapping lumps with a ragged rim, lighter towards the top left,
// stony spots (secondary) here and there.
QColor paintRawChunk(const QcTextureSpec &spec, int x, int y, QColor color)
{
    static constexpr std::array<std::array<double, 3>, 3> lumps = {{{6.5, 8.5, 4.4}, {10.0, 6.0, 3.4}, {9.5, 10.5, 3.6}}};
    double nearest = 2.0;
    for (const auto &[cx, cy, radius] : lumps) {
        const double distance = std::hypot(x - cx, y - cy) / radius + 0.12 * noise(spec.seed + 5, x, y);
        nearest = std::min(nearest, distance);
    }
    if (nearest > 1.0) {
        return clear();
    }

    if (nearest > 0.8) {
        return scaled(spec.secondary, 0.85);
    }
    if (hashPixel(spec.seed + 7, x, y) % 6 == 0) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return scaled(color, 1.12 - 0.02 * (x + y - 8));
}

// Block of raw metal: rounded lumps around jittered points of a 4 x 4 grid (wrapping, so the face
// tiles), brighter in their middle, with crevices (secondary) where two lumps meet.
QColor paintRawBlock(const QcTextureSpec &spec, int x, int y, QColor color)
{
    constexpr int cell = 4;
    constexpr int cells = size / cell;
    double first = 1e9;
    double second = 1e9;
    for (int cy = -1; cy <= cells; ++cy) {
        for (int cx = -1; cx <= cells; ++cx) {
            const quint32 jitter = hashPixel(spec.seed + 11, (cx + cells) % cells, (cy + cells) % cells);
            const double px = cx * cell + 0.5 + static_cast<double>(jitter % cell);
            const double py = cy * cell + 0.5 + static_cast<double>((jitter / cell) % cell);
            const double distance = std::hypot(x - px, y - py);
            second = std::min(second, std::max(first, distance));
            first = std::min(first, distance);
        }
    }

    if (second - first < 0.9) {
        return scaled(spec.secondary, 0.9);
    }
    if (hashPixel(spec.seed + 13, x, y) % 7 == 0) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return scaled(color, 1.15 - 0.1 * first);
}

// Chain links along `along` (the running direction) through the middle four pixels of `across`:
// a link seen face on (a ring around a hole) and one seen edge on (a short bar), every 8 pixels.
QColor paintChainLinks(const QcTextureSpec &spec, int along, int across)
{
    if (across < 6 || across > 9) {
        return clear();
    }

    const int step = along % 8;
    const bool middle = across == 7 || across == 8;
    const bool ring = step <= 4 && (step == 0 || step == 4 ? middle : !middle);
    const bool bar = step > 4 && middle;
    if (!ring && !bar) {
        return clear();
    }
    if (across == 6 || (bar && across == 7)) {
        return varied(spec, spec.secondary, along, across, 3);
    }
    return across == 9 ? scaled(spec.primary, 0.75) : varied(spec, spec.primary, along, across);
}

QColor paintChain(const QcTextureSpec &spec, int x, int y, QColor)
{
    return paintChainLinks(spec, y, x);
}

QColor paintChainAcross(const QcTextureSpec &spec, int x, int y, QColor)
{
    return paintChainLinks(spec, x, y);
}

// The faces of a leather backpack (decision E-832) inside the seam (backpackSeam): a strap with a
// buckle (side), a stitched flap with its buckle (front) or a carrying loop (top).
QColor paintBackpackSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool strap = x >= 6 && x <= 9;
    const bool buckle = strap && (y == 9 || y == 10) && (x == 6 || x == 9 || y == 9);
    if (buckle) {
        return varied(spec, spec.secondary, x, y, 7);
    }
    return strap ? scaled(spec.primary, 0.78) : color;
}

QColor paintBackpackFront(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool flap = y <= 6;
    const bool flapEdge = y == 6 || (flap && (x == 2 || x == size - 3));
    const bool buckle = x >= 7 && x <= 8 && y >= 5 && y <= 8;
    if (buckle) {
        return varied(spec, spec.secondary, x, y, 7);
    }
    if (flapEdge) {
        return scaled(spec.primary, 0.7);
    }
    return flap ? scaled(spec.primary, 0.9) : color;
}

QColor paintBackpackTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool loop = (y == 7 || y == 8) && x >= 4 && x <= 11;
    return loop ? scaled(spec.primary, 0.72) : color;
}

// Every face of the backpack: a darker seam with light stitches at the edges around the face's own
// pattern.
QColor paintBackpack(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool seam = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    if (seam) {
        return scaled(spec.primary, 0.62);
    }
    const bool stitch = (x == 1 || y == 1 || x == size - 2 || y == size - 2) && (x + y) % 3 == 0;
    if (stitch) {
        return scaled(spec.primary, 1.3);
    }
    if (spec.pattern == Pattern::BackpackSide) {
        return paintBackpackSide(spec, x, y, color);
    }
    return spec.pattern == Pattern::BackpackFront ? paintBackpackFront(spec, x, y, color) : paintBackpackTop(spec, x, y, color);
}

// The inside of the backpack's icon: a lighter flap reaching lower in the middle, its buckle
// (secondary) and a stitched pocket below.
QColor paintBackpackItemFace(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool buckle = (x == 7 || x == 8) && y >= 8 && y <= 10;
    if (buckle) {
        return (x + y) % 2 == 0 ? spec.secondary : scaled(spec.secondary, 0.8);
    }

    const int flapEnd = x >= 5 && x <= 10 ? 9 : 8;
    if (y == flapEnd) {
        return scaled(spec.primary, 0.7);
    }
    if (y < flapEnd) {
        return y == 5 && x % 2 == 0 ? scaled(spec.primary, 1.45) : scaled(color, 1.2);
    }

    const bool pocket = x >= 5 && x <= 10 && y >= 11 && y <= 13;
    const bool pocketEdge = pocket && (x == 5 || x == 10 || y == 11);
    if (pocketEdge) {
        return scaled(spec.primary, 0.72);
    }
    return pocket ? scaled(color, 1.08) : color;
}

// The backpack's icon (decision E-832): a leather body with rounded corners and a dark outline
// under a carrying loop, its face painted by paintBackpackItemFace.
QColor paintBackpackItem(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool loop = y >= 1 && y <= 3 && (x == 6 || x == 9 || (y == 1 && (x == 7 || x == 8)));
    if (loop) {
        return scaled(spec.primary, 0.6);
    }

    const bool inside = x >= 3 && x <= 12 && y >= 4 && y <= 14;
    const bool corner = (x == 3 || x == 12) && (y == 4 || y == 14);
    if (!inside || corner) {
        return clear();
    }
    const bool rim = x == 3 || x == 12 || y == 4 || y == 14;
    const bool rounding = (x == 4 || x == 11) && (y == 5 || y == 13);
    if (rim || rounding) {
        return scaled(spec.primary, 0.55);
    }
    return paintBackpackItemFace(spec, x, y, color);
}

constexpr std::array<std::pair<Pattern, Painter>, 27> painters = {{
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
    {Pattern::RawChunk, paintRawChunk},
    {Pattern::RawBlock, paintRawBlock},
    {Pattern::Chain, paintChain},
    {Pattern::ChainAcross, paintChainAcross},
    {Pattern::BackpackSide, paintBackpack},
    {Pattern::BackpackFront, paintBackpack},
    {Pattern::BackpackTop, paintBackpack},
    {Pattern::BackpackItem, paintBackpackItem},
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
