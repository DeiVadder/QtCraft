// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureRangedPatterns.h"

#include <QPointF>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
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

// Whether (x, y) lies on the thin line from `from` to `to`.
bool onLine(int x, int y, QPointF from, QPointF to)
{
    const QPointF way = to - from;
    const QPointF point = QPointF(x, y) - from;
    const double along = QPointF::dotProduct(point, way) / QPointF::dotProduct(way, way);
    const QPointF nearest = from + way * std::clamp(along, 0.0, 1.0);
    return std::hypot(x - nearest.x(), y - nearest.y()) <= 0.5;
}

// The bow drawn `stage` (1..3) texels back along the diagonal to the bottom right: the limbs bend
// tighter around the same grip, the string (secondary) runs from their tips to the nock, and the
// arrow lies on the diagonal with its stone head beyond the grip and its fletching (secondary) at
// the nock.
QColor paintDrawnBow(const QcTextureSpec &spec, int x, int y, QColor color, int stage)
{
    constexpr double gripAt = 13.5 - 11.0 / std::numbers::sqrt2;
    constexpr int arrowLength = 9;
    constexpr QColor stone(0x6e, 0x6e, 0x72);
    const int nock = 8 + stage;
    const int head = nock - arrowLength;
    const int side = std::abs(x - y);
    // The head: a point with two barbs along the edges of a corner.
    if ((side == 0 && x >= head && x <= head + 1) || (side >= 1 && side <= 2 && std::min(x, y) == head)) {
        return varied(spec, stone, x, y, 8);
    }
    if (side == 1 && std::max(x, y) >= nock - 1 && std::max(x, y) <= nock) {
        return varied(spec, spec.secondary, x, y, 6);
    }
    if (side == 0 && x > head && x <= nock) {
        return varied(spec, scaled(color, 1.25), x, y, 5);
    }

    const double radius = 11.0 - 0.5 * stage;
    const double centre = gripAt + radius / std::numbers::sqrt2;
    // The limb keeps its length: the tighter the bend, the wider the arc.
    const double half = 9.6 / radius;
    const double tipAcross = radius * std::sin(half) / std::numbers::sqrt2;
    const double tipAlong = centre - radius * std::cos(half) / std::numbers::sqrt2;
    if (std::abs(std::hypot(x - centre, y - centre) - radius) <= 0.85 && x + y <= 2.0 * tipAlong + 0.5) {
        return varied(spec, side <= 1 ? scaled(color, 0.65) : color, x, y, 6);
    }
    const QPointF nockPoint(nock, nock);
    const bool string = onLine(x, y, {tipAlong + tipAcross, tipAlong - tipAcross}, nockPoint)
                        || onLine(x, y, {tipAlong - tipAcross, tipAlong + tipAcross}, nockPoint);
    return string ? spec.secondary : clear();
}

template <int Stage>
QColor paintBowDrawn(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintDrawnBow(spec, x, y, color, Stage);
}

// The colours of an arrow's parts.
struct ArrowColours
{
    QColor head;
    QColor shaft;
    QColor fletching;
};

// A shaft with a head at the top right and fletching at the bottom left.
QColor paintArrowShape(const QcTextureSpec &spec, int x, int y, const ArrowColours &colours)
{
    const double along = alongOf(x, y);
    const double across = std::abs(acrossOf(x, y));
    if (along >= 4.0 && along <= 7.0 && across <= (7.0 - along) * 0.8 + 0.5) {
        return varied(spec, colours.head, x, y, 8);
    }
    if (along >= -6.5 && along < 4.0 && across <= 0.5) {
        return varied(spec, colours.shaft, x, y, 5);
    }
    if (along >= -7.0 && along <= -3.5 && across <= 1.5) {
        return varied(spec, colours.fletching, x, y, 6);
    }
    return clear();
}

// A shaft (primary) with a stone head and white fletching (secondary).
QColor paintArrow(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintArrowShape(spec, x, y, {QColor(0x6e, 0x6e, 0x72), color, spec.secondary});
}

// A tipped arrow (decision E-1102): a shaft (primary) whose head and the piece behind it are wet
// with the potion (secondary), and white fletching, fixed like the plain arrow's head: a texture
// has two colours, and the potion takes the second.
QColor paintTippedArrow(const QcTextureSpec &spec, int x, int y, QColor color)
{
    constexpr double wetFrom = 1.0;
    const QColor shaft = alongOf(x, y) >= wetFrom ? spec.secondary : color;
    return paintArrowShape(spec, x, y, {spec.secondary, shaft, QColor(0xe8, 0xe8, 0xe8)});
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

constexpr std::array<std::pair<Pattern, Painter>, 9> painters = {{
    {Pattern::Bow, paintBow},
    {Pattern::BowDrawn1, paintBowDrawn<1>},
    {Pattern::BowDrawn2, paintBowDrawn<2>},
    {Pattern::BowDrawn3, paintBowDrawn<3>},
    {Pattern::Arrow, paintArrow},
    {Pattern::TippedArrow, paintTippedArrow},
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
