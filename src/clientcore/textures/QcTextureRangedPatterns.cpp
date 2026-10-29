// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureRangedPatterns.h"

#include <array>
#include <cmath>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

// The icons lie on the diagonal from the bottom left to the top right: `along` runs along it
// (positive towards the top right), `across` across it, both in pixels.
double alongOf(int x, int y)
{
    return (x - y) / 2.0;
}
double acrossOf(int x, int y)
{
    return (x + y - (size - 1)) / 2.0;
}

// A wooden limb (primary) bent towards the top left, its grip darker, and the string (secondary)
// straight between its ends.
QColor paintBow(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 13.5;
    const double dy = y - 13.5;
    const double radius = std::sqrt(dx * dx + dy * dy);
    const bool limb = std::abs(radius - 11.0) <= 0.85 && x + y <= 17;
    if (limb) {
        const bool grip = std::abs(x - y) <= 1;
        return varied(spec, grip ? scaled(color, 0.65) : color, x, y, 6);
    }
    const bool string = x + y == 16 && x >= 3 && x <= 13;
    return string ? spec.secondary : clear();
}

// A shaft (primary) with a stone head at the top right and white fletching (secondary) at the
// bottom left.
QColor paintArrow(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double along = alongOf(x, y);
    const double across = std::abs(acrossOf(x, y));
    if (along >= 4.0 && along <= 7.0 && across <= (7.0 - along) * 0.8 + 0.5) {
        return varied(spec, QColor(0x6e, 0x6e, 0x72), x, y, 8);
    }
    if (along >= -6.5 && along < 4.0 && across <= 0.5) {
        return varied(spec, color, x, y, 5);
    }
    if (along >= -7.0 && along <= -3.5 && across <= 1.5) {
        return varied(spec, spec.secondary, x, y, 6);
    }
    return clear();
}

// A quill (secondary) with soft vanes (primary) that narrow towards both ends.
QColor paintFeather(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double along = alongOf(x, y);
    const double across = std::abs(acrossOf(x, y));
    if (along < -7.0 || along > 7.0) {
        return clear();
    }
    if (across <= 0.5) {
        return spec.secondary;
    }
    const double width = along < -4.5 ? 0.0 : 2.6 * (1.0 - std::abs(along - 1.0) / 7.0);
    return across <= width ? varied(spec, color, x, y, 7) : clear();
}

// A bone (primary): a shaft with two round knobs at each end, the knobs' outer edge shaded by the
// secondary colour.
QColor paintBone(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double along = alongOf(x, y);
    const double across = acrossOf(x, y);
    if (std::abs(along) <= 5.0 && std::abs(across) <= 1.0) {
        return varied(spec, color, x, y, 5);
    }
    for (const double end : {-5.5, 5.5}) {
        for (const double side : {-1.3, 1.3}) {
            const double distance = std::hypot(along - end, across - side);
            if (distance <= 1.6) {
                return varied(spec, distance > 1.0 && std::abs(along) > std::abs(end) ? spec.secondary : color, x, y, 5);
            }
        }
    }
    return clear();
}

// A rod (primary, darker towards the grip at the bottom left) on the diagonal, its line (secondary)
// hanging straight down from the tip to a small hook.
QColor paintFishingRod(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double along = alongOf(x, y);
    if ((x + y == 15 || x + y == 16) && along >= -6.5 && along <= 6.5) {
        return varied(spec, along < -3.0 ? scaled(color, 0.7) : color, x, y, 5);
    }
    const bool line = x == 14 && y >= 2 && y <= 12;
    const bool hook = (y == 13 && (x == 13 || x == 14)) || (x == 12 && y == 12);
    return line || hook ? spec.secondary : clear();
}

constexpr std::array<std::pair<Pattern, Painter>, 5> painters = {{
    {Pattern::Bow, paintBow},
    {Pattern::Arrow, paintArrow},
    {Pattern::Feather, paintFeather},
    {Pattern::Bone, paintBone},
    {Pattern::FishingRod, paintFishingRod},
}};

} // namespace

QcTexturePaint::Painter qcRangedPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
