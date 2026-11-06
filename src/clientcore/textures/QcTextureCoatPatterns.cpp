// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureCoatPatterns.h"

#include <algorithm>
#include <cmath>

namespace {

using Pattern = QcTextureSpec::Pattern;

using namespace QcTexturePaint;

// The patches come from value noise on two lattices that wrap around the tile, so the texture
// repeats without seams: a coarse one of 3 x 3 cells gives a few big blotches per face, a fine one
// of 6 x 6 cells frays their edges.
constexpr int coarseCells = 3;
constexpr int fineCells = 6;
constexpr double fineWeight = 0.35;
// Where the noise lies above this, the secondary colour shows: about two fifths of the face.
constexpr double patchThreshold = 0.55;
constexpr quint32 fineSalt = 11;

int wrapped(int index, int cells)
{
    return ((index % cells) + cells) % cells;
}

// Value of a coarse lattice point in [0, 1]: its rank among the hashes of all points, so every face
// has both low and high points and shows both colours.
double coarseValue(quint32 seed, int i, int j)
{
    const int column = wrapped(i, coarseCells);
    const int row = wrapped(j, coarseCells);
    const quint32 own = hashPixel(seed, column, row);
    const int ownIndex = row * coarseCells + column;
    int rank = 0;
    for (int b = 0; b < coarseCells; ++b) {
        for (int a = 0; a < coarseCells; ++a) {
            const quint32 other = hashPixel(seed, a, b);
            const int otherIndex = b * coarseCells + a;
            if (other < own || (other == own && otherIndex < ownIndex)) {
                ++rank;
            }
        }
    }
    return static_cast<double>(rank) / (coarseCells * coarseCells - 1);
}

double fineValue(quint32 seed, int i, int j)
{
    return (noise(seed + fineSalt, wrapped(i, fineCells), wrapped(j, fineCells)) + 1.0) / 2.0;
}

double smoothStep(double t)
{
    return t * t * (3.0 - 2.0 * t);
}

using LatticeValue = double (*)(quint32 seed, int i, int j);

// Value noise at the centre of pixel (x, y): the lattice values around it, blended smoothly.
double valueNoise(LatticeValue value, quint32 seed, int cells, int x, int y)
{
    const double u = (x + 0.5) * cells / size;
    const double v = (y + 0.5) * cells / size;
    const int i = static_cast<int>(std::floor(u));
    const int j = static_cast<int>(std::floor(v));
    const double fu = smoothStep(u - i);
    const double fv = smoothStep(v - j);

    const double top = value(seed, i, j) * (1.0 - fu) + value(seed, i + 1, j) * fu;
    const double bottom = value(seed, i, j + 1) * (1.0 - fu) + value(seed, i + 1, j + 1) * fu;
    return top * (1.0 - fv) + bottom * fv;
}

// Large irregular patches (secondary) on the primary colour, a few per face; the seed places them.
QColor paintPatches(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double coarse = valueNoise(coarseValue, spec.seed, coarseCells, x, y);
    const double fine = valueNoise(fineValue, spec.seed, fineCells, x, y);
    const double field = (1.0 - fineWeight) * coarse + fineWeight * fine;
    return field > patchThreshold ? varied(spec, spec.secondary, x, y, 3) : color;
}

// The faint glow deep in the eye sockets of a skull.
constexpr QRgb skullGlow = 0x6e2c12;

// The front of a skull, the same on both sides: a bare brow over two round sockets (secondary), a
// nose hole, and a row of teeth above and below a dark bite in a jaw narrower than the head.
QColor paintSkull(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int side = std::min(x, 15 - x);
    const bool corner = (y == 5 || y == 8) && (side == 2 || side == 6);
    const bool socket = y >= 5 && y <= 8 && side >= 2 && side <= 6 && !corner;
    const bool nose = (y == 9 && side == 7) || (y == 10 && side >= 6);
    const bool outsideJaw = y >= 12 && side < 2;
    const bool bite = y == 13 || (y >= 12 && side % 2 == 1);
    if (socket && y == 7 && (side == 4 || side == 5)) {
        return QColor(skullGlow);
    }
    if (socket || nose || outsideJaw) {
        return varied(spec, spec.secondary, x, y, 2);
    }
    if (y >= 12) {
        return bite ? varied(spec, spec.secondary, x, y, 2) : scaled(color, 1.7);
    }
    return y == 4 && side >= 2 && side <= 6 ? scaled(color, 1.3) : color;
}

} // namespace

QcTexturePaint::Painter qcCoatPatternPainter(QcTextureSpec::Pattern pattern)
{
    if (pattern == Pattern::Skull) {
        return paintSkull;
    }
    return pattern == Pattern::Patches ? paintPatches : nullptr;
}
