// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureArmourPatterns.h"

#include <array>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

using Shape = bool (*)(int x, int y);

bool inside(int x, int y)
{
    return x >= 0 && y >= 0 && x < size && y < size;
}

bool helmetShape(int x, int y)
{
    const bool dome = y >= 3 && y <= 7 && x >= 3 && x <= 12 && !(y == 3 && (x == 3 || x == 12));
    const bool cheeks = y >= 8 && y <= 11 && ((x >= 3 && x <= 5) || (x >= 10 && x <= 12));
    return dome || cheeks;
}

bool chestplateShape(int x, int y)
{
    const bool shoulders = y >= 2 && y <= 7 && ((x >= 1 && x <= 5) || (x >= 10 && x <= 14));
    const bool body = y >= 4 && y <= 14 && x >= 3 && x <= 12 && !(y == 4 && x >= 6 && x <= 9);
    return shoulders || body;
}

bool leggingsShape(int x, int y)
{
    const bool waist = y >= 2 && y <= 4 && x >= 3 && x <= 12;
    const bool legs = y >= 5 && y <= 14 && ((x >= 3 && x <= 6) || (x >= 9 && x <= 12));
    return waist || legs;
}

bool bootsShape(int x, int y)
{
    const bool shafts = y >= 6 && y <= 11 && ((x >= 2 && x <= 5) || (x >= 10 && x <= 13));
    const bool toes = y >= 12 && y <= 13 && ((x >= 1 && x <= 6) || (x >= 9 && x <= 14));
    return shafts || toes;
}

// An icon of `shape`: dark outline, light upper edge in the secondary colour, the material inside.
QColor paintIcon(const QcTextureSpec &spec, int x, int y, QColor color, Shape shape)
{
    if (!shape(x, y)) {
        return clear();
    }

    const auto filled = [shape](int px, int py) { return inside(px, py) && shape(px, py); };
    if (!filled(x - 1, y) || !filled(x + 1, y) || !filled(x, y + 1)) {
        return scaled(spec.primary, 0.6);
    }
    if (!filled(x, y - 1) || !filled(x - 1, y - 1)) {
        return varied(spec, spec.secondary, x, y, 4);
    }
    return color;
}

QColor paintHelmet(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintIcon(spec, x, y, color, helmetShape);
}

QColor paintChestplate(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintIcon(spec, x, y, color, chestplateShape);
}

QColor paintLeggings(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintIcon(spec, x, y, color, leggingsShape);
}

QColor paintBoots(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintIcon(spec, x, y, color, bootsShape);
}

// A plate stretched over a box face: dark rim, a light bevel at the top and left, a seam across
// the middle and a rivet in each corner.
QColor paintArmourPlate(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rim = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    const bool rivet = (x == 2 || x == size - 3) && (y == 2 || y == size - 3);
    if (rim) {
        return scaled(spec.primary, 0.62);
    }
    if (rivet || x == 1 || y == 1) {
        return varied(spec, spec.secondary, x, y, 2);
    }
    if (x == size - 2 || y == size - 2 || y == 8) {
        return scaled(spec.primary, 0.82);
    }
    return color;
}

// Hide stretched over a box face with a stitched seam along the rim.
QColor paintArmourLeather(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rim = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    const bool seam = (x == 1 || y == 1 || x == size - 2 || y == size - 2) && (x + y) % 3 == 0 && !rim;
    if (rim) {
        return scaled(spec.primary, 0.7);
    }
    if (seam) {
        return varied(spec, spec.secondary, x, y, 6);
    }
    return color;
}

// The front of a helmet: a plate with an opening for the face (transparent).
QColor paintHelmetFront(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (x >= 3 && x <= 12 && y >= 6 && y <= 14) {
        return clear();
    }
    return paintArmourPlate(spec, x, y, color);
}

// A closed visor: a plate with two dark eye slits and breathing holes.
QColor paintVisor(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool slit = (y == 6 || y == 7) && x >= 2 && x <= 13 && x != 7 && x != 8;
    const bool holes = y >= 10 && y <= 12 && x >= 4 && x <= 11 && (x + y) % 2 == 0;
    if (slit) {
        return QColor(20, 20, 26);
    }
    if (holes) {
        return scaled(spec.primary, 0.45);
    }
    return paintArmourPlate(spec, x, y, color);
}

// Trim of shoulder pieces, crests and caps: polished, with a raised ridge along the middle.
QColor paintArmourTrim(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rim = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    if (rim) {
        return scaled(spec.primary, 0.65);
    }
    if (y == 7 || y == 8) {
        return varied(spec, spec.secondary, x, y, 7);
    }
    return y < 7 ? scaled(color, 1.08) : color;
}

// Armour stand icon: a wooden post with shoulder and hip bars (primary) on a stone plate (secondary).
QColor paintArmourStand(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool plate = y >= 13 && y <= 14 && x >= 2 && x <= 13;
    const bool post = x >= 7 && x <= 8 && y >= 1 && y <= 12;
    const bool shoulders = y >= 4 && y <= 5 && x >= 3 && x <= 12;
    const bool arms = (x == 3 || x == 12) && y >= 6 && y <= 9;
    const bool hips = y == 8 && x >= 5 && x <= 10;
    const bool legs = (x == 5 || x == 10) && y >= 9 && y <= 12;
    if (plate) {
        return y == 14 ? scaled(spec.secondary, 0.7) : varied(spec, spec.secondary, x, y, 5);
    }
    if (post || shoulders || arms || hips || legs) {
        return x == 8 || y == 5 ? scaled(color, 0.8) : color;
    }
    return clear();
}

constexpr std::array<std::pair<Pattern, Painter>, 10> painters = {{
    {Pattern::ArmourStand, paintArmourStand},
    {Pattern::Helmet, paintHelmet},
    {Pattern::Chestplate, paintChestplate},
    {Pattern::Leggings, paintLeggings},
    {Pattern::Boots, paintBoots},
    {Pattern::ArmourPlate, paintArmourPlate},
    {Pattern::ArmourLeather, paintArmourLeather},
    {Pattern::HelmetFront, paintHelmetFront},
    {Pattern::Visor, paintVisor},
    {Pattern::ArmourTrim, paintArmourTrim},
}};

} // namespace

QcTexturePaint::Painter qcArmourPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
