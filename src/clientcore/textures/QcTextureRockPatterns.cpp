// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureRockPatterns.h"

#include "clientcore/textures/QcTextureField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

namespace {

using namespace QcTextureField;
using Pattern = QcTextureSpec::Pattern;
using QcTexturePaint::scaled;
using QcTextureRelief::Entry;
using QcTextureRelief::Surface;

// Scattered features of a tile.
constexpr int hollowCount = 9;
constexpr int crystalCount = 7;
constexpr int strandCount = 5;
constexpr int flakeCount = 8;

// How deep a pore of fused rock is at `point`: 0 outside, 1 at its bottom. Few cells hold one.
double poreDepth(quint32 seed, QPointF point)
{
    const Cell pore = cellAt(seed + 4, point, {6, 6, 0.9, false});
    if (pore.id % 8 >= 3) {
        return 0.0;
    }
    const double radius = 0.7 + 0.8 * unit(pore.id >> 8);
    return 1.0 - smooth(radius * 0.45, radius, pore.distance);
}

// The rock's lumps: their border, and how open the seam around them is at this point. Where two
// lumps melted together there is no seam.
struct Seam
{
    Cell lump;
    double open = 0.0;
};

Seam seamAt(quint32 seed, QPointF point)
{
    const Cell lump = cellAt(seed + 2, warped(seed + 1, point, 8.0, 2.6), {4, 4, 0.95, false});
    const double width = 0.1 + 0.8 * smooth(-0.1, 0.4, fractal(seed + 3, point, 4.0, 2));
    return {lump, 1.0 - smooth(width * 0.4, width, lump.edge)};
}

// Porous rock fused of crooked lumps; ores are set into it.
Surface fusedRock(quint32 seed, const QColor &body, const QColor &dark, QPointF point)
{
    const Seam seam = seamAt(seed, point);
    const double pore = poreDepth(seed, point);
    const double rough = fractal(seed + 5, point, 2.0, 3);
    const double height = 1.6 * smooth(0.0, 2.4, seam.lump.edge) - 1.2 * seam.open - 2.0 * pore + 0.5 * rough;
    const double tone = 0.9 + 0.2 * unit(seam.lump.id) + 0.2 * rough;
    return {height, mixed(scaled(body, tone), dark, std::max(seam.open, 0.92 * pore))};
}

Surface paintFusedRock(const QcTextureSpec &spec, QPointF point)
{
    return fusedRock(spec.seed, spec.primary, spec.secondary, point);
}

// How deep a hollow of sand is at `point`: 0 outside, 1 at its bottom. Hollows are small uneven
// bowls scattered without a pattern, a few of them larger.
double hollowDepth(quint32 seed, QPointF point)
{
    const QPointF bent = warped(seed + 4, point, 4.0, 0.7);
    double depth = 0.0;
    for (int index = 0; index < hollowCount; ++index) {
        const double share = unit(QcTexturePaint::hashPixel(seed + 5, index, 0));
        const double radius = 0.8 + 1.5 * share * share;
        const double away = length(wrappedOffset(bent, scattered(seed + 3, index)));
        depth = std::max(depth, 1.0 - smooth(radius * 0.25, radius, away));
    }
    return depth;
}

Surface paintHollowSand(const QcTextureSpec &spec, QPointF point)
{
    const Cell grain = cellAt(spec.seed + 1, point, {16, 16, 0.95, false});
    const double hollow = hollowDepth(spec.seed, point);
    const double drift = fractal(spec.seed + 2, point, 8.0, 2);

    // Grains stand out singly: a few pale, a few dark, the crowd between them.
    const double kind = unit(grain.id);
    const double tone = kind > 0.85 ? 1.26 : (kind < 0.18 ? 0.74 : 0.92 + 0.16 * unit(grain.id >> 8));
    const double dome = smooth(0.0, 0.5, grain.edge);
    const double height = 1.2 * drift + 0.35 * dome - 3.2 * hollow;
    return {height, mixed(scaled(spec.primary, tone * (1.0 + 0.3 * drift)), spec.secondary, smooth(0.15, 0.8, hollow))};
}

// Pale grit stuck in the soil at `point`: 0 or 1.
double gritAt(quint32 seed, QPointF point)
{
    const Cell grit = cellAt(seed + 6, point, {8, 8, 0.9, false});
    return grit.id % 5 == 0 ? 1.0 - smooth(0.35, 0.65, grit.distance) : 0.0;
}

Surface paintCrustedSoil(const QcTextureSpec &spec, QPointF point)
{
    const Cell plate = cellAt(spec.seed + 1, point, {3, 3, 0.9, false});
    const double crack = 1.0 - smooth(0.15, 0.55, plate.edge);

    // Each plate tilts its own way, so the plates differ from each other in light.
    const double turn = 2.0 * std::numbers::pi * unit(plate.id >> 4);
    const QPointF away = point - plate.centre;
    const double tilt = 0.16 * (away.x() * std::cos(turn) + away.y() * std::sin(turn));
    const double rough = fractal(spec.seed + 2, point, 2.0, 3);
    const double grit = gritAt(spec.seed, point);
    const double height = tilt + 0.25 * rough - 1.2 * crack + 0.4 * grit;
    const QColor soil = scaled(spec.primary, 0.84 + 0.34 * unit(plate.id) + 0.14 * rough);
    return {height, mixed(mixed(soil, spec.secondary, 0.9 * grit), scaled(spec.primary, 0.42), 0.9 * crack)};
}

// A slim spindle lying at `angle` around `centre`, its ends `reach` from the centre, half as wide
// as `width` across: how far inside `point` lies (1 and more outside) and on which side of the
// long axis.
struct Spindle
{
    double shape = 2.0;
    double side = 0.0;
};

Spindle spindleAt(QPointF point, QPointF centre, double angle, QSizeF extent)
{
    const QPointF away = wrappedOffset(centre, point);
    const double along = away.x() * std::cos(angle) + away.y() * std::sin(angle);
    const double across = -away.x() * std::sin(angle) + away.y() * std::cos(angle);
    const double taper = (std::abs(along) + 1.4 * std::abs(across)) / extent.width();
    return {std::max(std::abs(across) / extent.height(), taper), across};
}

// The crystal nearest to `point`: two large ones, the rest splinters, scattered over the tile
// without a pattern and turned every way.
Spindle crystalAt(quint32 seed, QPointF point)
{
    Spindle nearest;
    for (int index = 0; index < crystalCount; ++index) {
        const quint32 id = QcTexturePaint::hashPixel(seed + 7, index, 1);
        const double reach = index < 2 ? 3.0 + 1.4 * unit(id) : 1.2 + 1.2 * unit(id);
        const double angle = std::numbers::pi * unit(id >> 16);
        const Spindle crystal = spindleAt(point, scattered(seed + 8, index), angle, QSizeF(reach, 0.27 * reach));
        if (crystal.shape < nearest.shape) {
            nearest = crystal;
        }
    }
    return nearest;
}

Surface paintQuartzOre(const QcTextureSpec &spec, QPointF point)
{
    const Surface rock = fusedRock(spec.seed, spec.primary, scaled(spec.primary, 0.42), point);
    const Spindle crystal = crystalAt(spec.seed, point);
    const double inside = 1.0 - smooth(0.92, 1.05, crystal.shape);
    if (inside <= 0.0) {
        return rock;
    }
    const QColor shard = crystal.side < 0.0 ? scaled(spec.secondary, 1.05) : scaled(spec.secondary, 0.74);
    return {rock.height + 2.0 * inside, mixed(rock.colour, shard, inside)};
}

// How much gold lies at `point`: short crooked strands of different lengths and single flakes,
// scattered over the tile without a pattern.
double goldAt(quint32 seed, QPointF point)
{
    const QPointF bent = warped(seed + 9, point, 4.0, 2.4);
    double gold = 0.0;
    for (int index = 0; index < strandCount; ++index) {
        const quint32 id = QcTexturePaint::hashPixel(seed + 10, index, 0);
        const double reach = 1.6 + 2.6 * unit(id);
        const Spindle strand = spindleAt(bent, scattered(seed + 11, index), std::numbers::pi * unit(id >> 16), QSizeF(reach, 0.45));
        gold = std::max(gold, 1.0 - smooth(0.8, 1.05, strand.shape));
    }
    for (int index = 0; index < flakeCount; ++index) {
        const double radius = 0.4 + 0.5 * unit(QcTexturePaint::hashPixel(seed + 12, index, 0));
        const double away = length(wrappedOffset(point, scattered(seed + 13, index)));
        gold = std::max(gold, 1.0 - smooth(radius * 0.7, radius, away));
    }
    return gold;
}

Surface paintGoldVeinOre(const QcTextureSpec &spec, QPointF point)
{
    const Surface rock = fusedRock(spec.seed, spec.primary, scaled(spec.primary, 0.42), point);
    const double gold = goldAt(spec.seed, point);
    const QColor metal = scaled(spec.secondary, 0.9 + 0.3 * noise(spec.seed + 14, point, 2.0));
    return {rock.height * (1.0 - gold) + 1.3 * gold, mixed(rock.colour, metal, gold)};
}

// The way to the nearest crack of the crust, and the plate's id. Large plates stay whole, some
// are broken into small ones, so the plates differ in size.
Cell crustPlateAt(quint32 seed, QPointF point)
{
    const QPointF bent = warped(seed + 6, warped(seed + 2, point, 8.0, 1.8), 2.0, 0.5);
    const Cell large = cellAt(seed + 1, bent, {3, 3, 0.95, false});
    if (large.id % 3 != 0) {
        return large;
    }
    Cell small = cellAt(seed + 5, bent, {5, 5, 0.95, false});
    small.edge = std::min(small.edge, large.edge);
    return small;
}

Surface paintMagmaCrust(const QcTextureSpec &spec, QPointF point)
{
    const Cell plate = crustPlateAt(spec.seed, point);

    // The cracks are thin, widen in places and close in others; molten rock is palest in their
    // middle.
    const double opening = fractal(spec.seed + 3, point, 4.0, 2);
    const double width = (0.32 + 0.5 * smooth(0.0, 0.6, opening)) * smooth(-0.3, 0.0, opening);
    const double molten = width > 0.02 ? 1.0 - smooth(0.0, width, plate.edge) : 0.0;
    const double rough = fractal(spec.seed + 4, point, 2.0, 3);
    const QColor crust = scaled(spec.primary, 0.85 + 0.45 * unit(plate.id) + 0.5 * rough);
    const QColor glow = mixed(spec.secondary, QColor(255, 226, 120), smooth(0.8, 1.0, molten));
    const double height = 1.3 * smooth(0.0, 2.0, plate.edge) + 0.5 * rough - 1.2 * molten;
    return {height, mixed(crust, glow, smooth(0.1, 0.5, molten))};
}

Surface paintGlowCluster(const QcTextureSpec &spec, QPointF point)
{
    const Cell lump = cellAt(spec.seed + 1, warped(spec.seed + 2, point, 8.0, 1.4), {4, 4, 0.95, false});

    // Light has no relief: each lump burns from its middle, heat runs from the dim gaps
    // (secondary) over the glow (primary) to a nearly white core, and nothing is shaded.
    const double core = (1.0 - smooth(0.0, 3.4, lump.distance)) * (0.6 + 0.4 * unit(lump.id));
    const double gap = 1.0 - smooth(0.1, 0.6, lump.edge);
    const double heat = 1.15 * core + 0.12 + 0.14 * fractal(spec.seed + 3, point, 4.0, 3) - 0.3 * gap;
    const QColor glow = mixed(spec.secondary, spec.primary, smooth(0.05, 0.5, heat));
    return {0.0, mixed(glow, QColor(255, 250, 226), smooth(0.45, 0.95, heat))};
}

constexpr std::array<Entry, 7> surfaces = {{
    {Pattern::FusedRock, paintFusedRock},
    {Pattern::HollowSand, paintHollowSand},
    {Pattern::CrustedSoil, paintCrustedSoil},
    {Pattern::QuartzOre, paintQuartzOre},
    {Pattern::GoldVeinOre, paintGoldVeinOre},
    {Pattern::MagmaCrust, paintMagmaCrust},
    {Pattern::GlowCluster, paintGlowCluster},
}};

} // namespace

std::span<const Entry> qcRockSurfaces()
{
    return surfaces;
}
