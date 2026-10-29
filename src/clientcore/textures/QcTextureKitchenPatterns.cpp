// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureKitchenPatterns.h"

#include <QColor>

#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

// The red of the berries on the cake and of its jam.
QColor berry()
{
    return QColor(196, 40, 48);
}

QColor paintCakeTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Frosting (primary) with a slightly darker rim and a few berries.
    if (x == 0 || y == 0 || x == size - 1 || y == size - 1) {
        return scaled(color, 0.9);
    }
    const bool dot = (x % 5 == 2 && y % 5 == 2) || (x % 5 == 4 && y % 5 == 0 && x > 2 && y > 2);
    return dot && hashPixel(spec.seed + 3, x, y) % 3 != 0 ? berry() : color;
}

QColor paintCakeSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // The cake is half a block tall: the upper half of the texture stays clear. Frosting (primary)
    // in the top two rows dripping down here and there, sponge (secondary) with a line of jam below.
    if (y < size / 2) {
        return clear();
    }
    const bool drip = y == size / 2 + 2 && hashPixel(spec.seed + 5, x, 0) % 3 == 0;
    if (y < size / 2 + 2 || drip) {
        return color;
    }
    if (y == size / 2 + 5) {
        return varied(spec, berry(), x, y, 9);
    }
    return varied(spec, spec.secondary, x, y, 7);
}

QColor paintCakeItem(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // A whole cake seen from the front and a little above: a frosted top ellipse over a sponge band.
    const double dx = (x - 7.5) / 6.5;
    const double topY = (y - 6.5) / 2.5;
    if (dx * dx + topY * topY <= 1.0) {
        return (x == 5 || x == 10) && y == 6 ? berry() : color;
    }
    if (std::abs(dx) <= 1.0 && y > 6 && y <= 12) {
        return y == 9 ? berry() : varied(spec, spec.secondary, x, y, 7);
    }
    return clear();
}

QColor paintCampfireItem(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Two crossed logs (primary, ends in secondary) under a flame.
    const double slope = (x - 7.5) * 0.35;
    const bool across = x >= 1 && x <= 14;
    const bool logA = across && std::abs(y - 12.0 - slope) < 0.8;
    const bool logB = across && std::abs(y - 12.0 + slope) < 0.8;
    if (logA || logB) {
        return x <= 1 || x >= 14 ? spec.secondary : color;
    }

    const double flameWidth = (y - 2) * 0.45;
    if (y >= 2 && y <= 11 && std::abs(x - 7.5) <= flameWidth) {
        const bool core = std::abs(x - 7.5) <= flameWidth * 0.45 && y >= 6;
        return core ? QColor(255, 226, 120) : QColor(240, 128, 36);
    }
    return clear();
}

QColor paintLilyPad(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // A round leaf with a narrow notch towards one corner and veins from its middle.
    const double dx = x - 7.5;
    const double dy = y - 7.5;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 7.3) {
        return clear();
    }

    constexpr double notch = std::numbers::pi / 4.0;
    const double angle = std::atan2(dy, dx);
    if (distance > 1.0 && std::abs(angle - notch) < 0.22) {
        return clear();
    }
    const double spokes = std::fmod(angle + std::numbers::pi, std::numbers::pi / 4.0);
    const bool vein = distance > 1.5 && distance < 6.5 && (spokes < 0.12 || spokes > std::numbers::pi / 4.0 - 0.12);
    if (vein || distance > 6.6) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return color;
}

constexpr std::array<std::pair<Pattern, Painter>, 5> painters = {{
    {Pattern::CakeTop, paintCakeTop},
    {Pattern::CakeSide, paintCakeSide},
    {Pattern::CakeItem, paintCakeItem},
    {Pattern::CampfireItem, paintCampfireItem},
    {Pattern::LilyPad, paintLilyPad},
}};

} // namespace

QcTexturePaint::Painter qcKitchenPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
