// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureBuildingPatterns.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

// Mortar of the brick pattern: rows of four pixels, every other row shifted by half a brick.
bool isMortar(int x, int y)
{
    const int offset = (y / 4) % 2 == 0 ? 0 : 4;
    return y % 4 == 3 || (x + offset) % 8 == 7;
}

QColor paintMossyBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Moss (secondary) grows in soft patches, most of all in the mortar.
    const double patch = noise(spec.seed + 11, x / 3, y / 3) + 0.4 * noise(spec.seed + 12, x, y) + (isMortar(x, y) ? 0.5 : 0.0);
    if (patch > 0.55) {
        return varied(spec, spec.secondary, x, y, 7);
    }
    return isMortar(x, y) ? scaled(spec.primary, 0.66) : color;
}

QColor paintCrackedBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (isMortar(x, y)) {
        return varied(spec, spec.secondary, x, y, 5);
    }

    // Two zigzag cracks through the bricks, in the mortar colour but darker.
    const bool first = y >= 1 && y <= 10 && x == 4 + (y / 2) % 2 + y / 5;
    const bool second = x >= 8 && x <= 14 && y == 9 + (x / 3) % 2 + (x - 8) / 4;
    if (first || second) {
        return scaled(spec.secondary, 0.75);
    }
    return color;
}

QColor paintChiseledBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Twice the distance from the centre along the larger axis: 1 in the middle, 15 at the edge.
    const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15));
    if (ring == 15) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (ring == 13) {
        return scaled(spec.primary, 1.12);
    }
    if (ring == 9 || ring == 3) {
        return scaled(spec.secondary, 0.9);
    }
    if (ring == 7 || ring == 1) {
        return scaled(spec.primary, 1.1);
    }
    return color;
}

QColor paintSmoothStone(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // A smooth face with a thin darker frame.
    const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    return edge ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintGlazedTerracotta(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // An ornament that is not symmetric: a frame on the top and left edges, a quarter ring around the
    // bottom-right corner and a diamond near the top-left one, so turned blocks next to each other
    // form larger figures.
    const int dx = size - 1 - x;
    const int dy = size - 1 - y;
    const int corner = dx * dx + dy * dy;
    const int diamond = std::abs(x - 4) + std::abs(y - 4);
    if (x == 0 || y == 0 || (corner >= 64 && corner <= 100)) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    if (corner < 16) {
        return scaled(spec.secondary, 0.8);
    }
    if (diamond <= 2) {
        return diamond == 2 ? scaled(spec.primary, 0.7) : varied(spec, spec.secondary, x, y, 5);
    }
    if (x == y && x > 6 && corner > 100) {
        return scaled(spec.primary, 0.82);
    }
    return color;
}

// The mask in wrapped() is a positive modulo only for a power-of-two tile size.
static_assert(size > 0 && (size & (size - 1)) == 0);

// A coordinate wrapped into the tile (also for negative values): the ice patterns continue across
// the edge into the next block, so a frozen lake shows no grid (decision E-860).
int wrapped(int value)
{
    return value & (size - 1);
}

// Whether (x, y) lies on one of the streaks of ice: short segments running from the bottom left to
// the top right, placed by the seed. On the wrapped tile a diagonal never ends at the edge.
bool onIceStreak(quint32 seed, int x, int y)
{
    // Per streak: its diagonal, where it starts along it and how long it is (in steps of two).
    static constexpr std::array<std::array<int, 3>, 4> streaks = {{{2, 0, 12}, {3, 3, 6}, {10, 8, 9}, {11, 12, 3}}};
    const int diagonal = wrapped(x + y);
    const int along = wrapped(x - y);
    const int shift = static_cast<int>(seed % size);
    for (const auto &[streakDiagonal, start, length] : streaks) {
        if (diagonal == wrapped(streakDiagonal + shift) && wrapped(along - start - 3 * shift) < length) {
            return true;
        }
    }
    return false;
}

QColor mixed(const QColor &first, const QColor &second, double share, int alpha)
{
    const auto channel = [share](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * share)); };
    return QColor(channel(first.red(), second.red()), channel(first.green(), second.green()), channel(first.blue(), second.blue()), alpha);
}

// Soft light and dark clouds over the face, on a grid of four pixels that tiles.
double iceCloud(const QcTextureSpec &spec, int x, int y)
{
    return 1.0 + spec.variation * noise(spec.seed + 5, x / 4, y / 4);
}

QColor paintIce(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // The primary colour's alpha decides how much of the water below shows through.
    const int alpha = spec.primary.alpha();
    if (onIceStreak(spec.seed, x, y)) {
        return mixed(color, varied(spec, spec.secondary, x, y, 3), 0.45, std::min(255, alpha + 30));
    }
    // A faint haze beside each streak softens its edge.
    if (onIceStreak(spec.seed, x - 1, y) || onIceStreak(spec.seed, x + 1, y)) {
        return mixed(color, spec.secondary, 0.15, std::min(255, alpha + 10));
    }
    return scaled(color, iceCloud(spec, x, y), alpha);
}

// Whether (x, y) lies on one of the fine cracks of packed ice: three short wandering lines, each a
// walk of single steps (down, sideways or diagonal, now and then turning back) from a start the seed
// picks.
bool onIceCrack(quint32 seed, int x, int y)
{
    constexpr int cracks = 3;
    constexpr int steps = 8;
    for (int crack = 0; crack < cracks; ++crack) {
        int cx = static_cast<int>(hashPixel(seed + 31, crack, 0) % size);
        int cy = static_cast<int>(hashPixel(seed + 37, crack, 0) % size);
        int sideways = crack % 2 == 0 ? 1 : -1;
        for (int step = 0; step < steps; ++step) {
            if (wrapped(cx) == x && wrapped(cy) == y) {
                return true;
            }
            const quint32 turn = hashPixel(seed + 41, crack, step);
            if (turn % 5 == 0) {
                sideways = -sideways;
            }
            cx += turn % 4 == 0 ? 0 : sideways;
            cy += turn % 4 == 1 ? 0 : 1;
        }
    }
    return false;
}

QColor paintPackedIce(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (onIceCrack(spec.seed, x, y)) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return scaled(color, iceCloud(spec, x, y));
}

QColor paintSlime(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rim = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    if (rim) {
        return scaled(varied(spec, spec.secondary, x, y, 3), 1.0, spec.secondary.alpha());
    }
    // Soft blotches in the jelly, on a grid of four pixels.
    return scaled(color, 1.0 + spec.variation * noise(spec.seed + 9, x / 4, y / 4), spec.primary.alpha());
}

// The whole door as an icon, half as wide as high (x 4..11): boards (primary) in a frame
// (secondary), a window of four panes in the upper half, a rail across the middle and the handle
// below it.
QColor paintDoorItem(const QcTextureSpec &spec, int x, int y, QColor)
{
    constexpr int left = 4;
    constexpr int right = 11;
    if (x < left || x > right) {
        return clear();
    }

    const bool window = x >= left + 2 && x <= right - 2 && y >= 2 && y <= 6 && y != 4;
    if (window) {
        return clear();
    }
    const bool frame = x == left || x == right || y == 0 || y == size - 1 || y == 8;
    const bool windowFrame = (x == left + 1 || x == right - 1) && y >= 1 && y <= 7;
    if (frame || windowFrame || (y == 4 && x > left && x < right) || y == 1 || y == 7) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (x == right - 1 && (y == 10 || y == 11)) {
        return QColor(70, 70, 76);
    }
    if (x == left + 3 || x == right - 3) {
        return scaled(spec.primary, 0.82);
    }
    return scaled(spec.primary, 1.0 + spec.variation * 0.5 * noise(spec.seed, x / 3, y));
}

constexpr std::array<std::pair<Pattern, Painter>, 9> painters = {{
    {Pattern::DoorItem, paintDoorItem},
    {Pattern::MossyBricks, paintMossyBricks},
    {Pattern::CrackedBricks, paintCrackedBricks},
    {Pattern::ChiseledBricks, paintChiseledBricks},
    {Pattern::SmoothStone, paintSmoothStone},
    {Pattern::GlazedTerracotta, paintGlazedTerracotta},
    {Pattern::Ice, paintIce},
    {Pattern::PackedIce, paintPackedIce},
    {Pattern::Slime, paintSlime},
}};

} // namespace

QcTexturePaint::Painter qcBuildingPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
