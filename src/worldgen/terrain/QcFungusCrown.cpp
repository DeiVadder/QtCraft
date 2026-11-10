// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcFungusCrown.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTreeLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

// The longest plant hanging from a rim.
constexpr int longestHanging = 5;
// The shares of the cap that glow and of the rim with something hanging from it.
constexpr double glowShare = 0.22;
constexpr double hangingShare = 0.3;
// A stem from this height on carries a shelf under its canopy.
constexpr int shelfHeight = 9;

double roll(quint64 seed, QcBlockPos pos, int salt)
{
    return qcPositionRandom(seed + static_cast<quint64>(salt), pos.x, pos.y, pos.z);
}

// A distance from the stem, in blocks.
double distance(int dx, int dz)
{
    return std::sqrt(static_cast<double>(dx * dx + dz * dz));
}

// Calls `visit(dx, dz)` for every column a cap of radius `radius` may touch.
template <typename Visit>
void eachColumn(int radius, Visit visit)
{
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            visit(dx, dz);
        }
    }
}

void hang(const QcTreeShape::Params &params, QcBlockPos from, quint64 seed, QcTreeLayout &layout)
{
    if (params.hanging == qcAirBlockId || roll(seed, from, 1) >= hangingShare) {
        return;
    }
    const int length = 1 + static_cast<int>(roll(seed, from, 2) * longestHanging);
    for (int step = 1; step <= length; ++step) {
        layout.leaf(from.offset(0, -step, 0), params.hanging, false);
    }
}

// A cell of the cap; on the inside now and then the glowing block instead of the flesh.
void capCell(const QcTreeShape::Params &params, QcBlockPos pos, quint64 seed, bool inside, QcTreeLayout &layout)
{
    const bool glows = inside && params.glow != qcAirBlockId && roll(seed, pos, 3) < glowShare;
    layout.leaf(pos, glows ? params.glow : params.leaves, false);
}

// Roots creeping out at the foot of the stem, unequal on the four sides.
void buttress(const QcTreeShape::Params &params, QcBlockPos base, quint64 seed, QcTreeLayout &layout)
{
    static constexpr std::array<std::pair<int, int>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (const auto &[dx, dz] : sides) {
        const QcBlockPos side = base.offset(dx, 0, dz);
        if (roll(seed, side, 4) < 0.55) {
            layout.leaf(side, params.trunk, false);
            if (roll(seed, side, 5) < 0.35) {
                layout.leaf(side.offset(0, 1, 0), params.trunk, false);
            }
        }
    }
}

// How wide the wavy outline of a canopy is towards (dx, dz): lobes that differ from fungus to
// fungus.
double canopyOutline(double radius, int dx, int dz, double phase)
{
    const double angle = std::atan2(static_cast<double>(dz), static_cast<double>(dx));
    return radius + 0.9 * std::sin(3.0 * angle + phase) + 0.5 * std::sin(5.0 * angle - phase);
}

void buildDisc(const QcTreeShape::Params &params, QcBlockPos centre, double radius, quint64 seed, QcTreeLayout &layout)
{
    const double phase = 2.0 * std::numbers::pi * roll(seed, centre, 5);
    eachColumn(QcFungusCrown::reach, [&](int dx, int dz) {
        const double away = distance(dx, dz);
        const double outline = canopyOutline(radius, dx, dz, phase);
        if (away > outline) {
            return;
        }

        const QcBlockPos top = centre.offset(dx, 0, dz);
        capCell(params, top, seed, false, layout);
        // The rim droops by a block or two, and the middle bulges up.
        if (away > outline - 1.2) {
            const int droop = 1 + static_cast<int>(roll(seed, top, 6) * 2.4);
            for (int step = 1; step <= droop; ++step) {
                capCell(params, top.offset(0, -step, 0), seed, step == droop, layout);
            }
            hang(params, top.offset(0, -droop, 0), seed, layout);
        } else if (away < radius * 0.45) {
            capCell(params, top.offset(0, 1, 0), seed, false, layout);
        }
    });
}

} // namespace

namespace QcFungusCrown {

void buildDome(const QcTreeShape::Params &params, QcBlockPos base, int stemHeight, quint64 seed, QcTreeLayout &layout)
{
    buttress(params, base, seed, layout);

    const int radius = std::clamp(2 + stemHeight / 3, 3, reach - 1);
    const int capHeight = radius + 1;
    const QcBlockPos top = base.offset(0, stemHeight, 0);
    for (int depth = 0; depth < capHeight; ++depth) {
        // A quarter of a circle from the top down, with the lowest layer as wide as the cap.
        const double share = static_cast<double>(depth + 1) / capHeight;
        const double layerRadius = radius * std::sqrt(share * (2.0 - share)) + 0.3;
        const bool lowest = depth == capHeight - 1;
        eachColumn(radius + 1, [&](int dx, int dz) {
            const QcBlockPos cell = top.offset(dx, -depth, dz);
            const double away = distance(dx, dz) + 0.9 * roll(seed, cell, 7) - 0.45;
            if (away > layerRadius || (lowest && roll(seed, cell, 8) < 0.3)) {
                return;
            }
            // Hollow inside: a shell one to two blocks thick, closed at the top.
            if (depth > 0 && away < layerRadius - 1.5) {
                return;
            }
            capCell(params, cell, seed, depth > 0 && away < layerRadius - 0.6, layout);
            if (lowest) {
                hang(params, cell, seed, layout);
            }
        });
    }
}

void buildCanopy(const QcTreeShape::Params &params, QcBlockPos base, int stemHeight, quint64 seed, QcTreeLayout &layout)
{
    buttress(params, base, seed, layout);

    const double radius = std::clamp(2.5 + stemHeight / 3.0, 3.5, static_cast<double>(reach) - 1.4);
    buildDisc(params, base.offset(0, stemHeight, 0), radius, seed, layout);
    if (stemHeight >= shelfHeight) {
        buildDisc(params, base.offset(0, stemHeight * 3 / 5, 0), radius * 0.55, seed + 1, layout);
    }
}

} // namespace QcFungusCrown
