// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureStonePatterns.h"

#include "clientcore/textures/QcTextureField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

namespace {

using namespace QcTextureField;
using Pattern = QcTextureSpec::Pattern;
using QcTexturePaint::hashPixel;
using QcTexturePaint::scaled;
using QcTexturePaint::size;
using QcTextureRelief::Entry;
using QcTextureRelief::Surface;

constexpr int columnCount = 3;

// The column around a point: its id, the place across it (-1 at the left joint, 1 at the right
// one) and the way to the nearest joint in pixels.
struct Column
{
    quint32 id = 0;
    double across = 0.0;
    double joint = 0.0;
};

// Where column `index` begins at row `y`: evenly spaced, each joint moved by the seed and bowing
// its own way on the way down.
double jointAt(quint32 seed, int index, double y, double bow)
{
    const int joint = index % columnCount;
    const double spacing = static_cast<double>(size) / columnCount;
    const double offset = 2.2 * (unit(hashPixel(seed, joint, 0)) - 0.5);
    const double bend = bow * noise(seed + 1 + static_cast<quint32>(joint), QPointF(0.0, y), 4.0);
    return index * spacing + offset + bend;
}

Column columnAt(quint32 seed, QPointF point, double bow)
{
    // Measured from the first joint, which may lie on either side of the tile's edge.
    const double y = point.y();
    const double first = jointAt(seed, 0, y, bow);
    const double x = first + std::fmod(point.x() - first + 2.0 * size, static_cast<double>(size));
    int index = 0;
    while (index + 1 < columnCount && x >= jointAt(seed, index + 1, y, bow)) {
        ++index;
    }
    const double left = jointAt(seed, index, y, bow);
    const double right = index + 1 == columnCount ? first + size : jointAt(seed, index + 1, y, bow);
    const double across = 2.0 * (x - left) / (right - left) - 1.0;
    return {hashPixel(seed + 2, index, 0), across, std::min(x - left, right - x)};
}

// A fracture across the column: 1 on the line. Each column breaks at its own height, slanted.
double fractureAt(quint32 seed, const Column &column, QPointF point)
{
    const quint32 id = hashPixel(seed + 3, static_cast<int>(column.id & 0xFFu), 0);
    const double row = 16.0 * unit(id) + 5.0 * (unit(id >> 16) - 0.5) * column.across;
    const double apart = std::abs(std::remainder(point.y() - row, static_cast<double>(size)));
    return 1.0 - smooth(0.15, 0.5, apart);
}

Surface paintColumnSide(const QcTextureSpec &spec, QPointF point)
{
    const Column column = columnAt(spec.seed, point, 0.7);
    const double joint = 1.0 - smooth(0.3, 0.8, column.joint);
    const double fracture = fractureAt(spec.seed, column, point);
    const double streak = 0.6 * noise(spec.seed + 4, point, QSizeF(1.0, 8.0)) + 0.3 * noise(spec.seed + 5, point, QSizeF(0.5, 4.0));

    // A column's face is two facets meeting in a ridge: one takes the light, the other does not.
    const double height = 3.0 * (1.0 - std::abs(column.across)) - joint - 0.8 * fracture + 0.25 * streak;
    const double tone = 0.84 + 0.34 * unit(column.id) + 0.24 * streak;
    return {height, mixed(scaled(spec.primary, tone), spec.secondary, std::max(joint, 0.7 * fracture))};
}

Surface paintColumnTop(const QcTextureSpec &spec, QPointF point)
{
    // Few, nearly regular hexagons, wide enough to show their six sides at 16 x 16.
    const Cell top = cellAt(spec.seed + 1, point, {2, 2, 0.12, true});
    const double joint = 1.0 - smooth(0.3, 0.75, top.edge);
    const double rough = fractal(spec.seed + 2, point, 2.0, 3);

    // The cut ends differ in tone; each has a step inside its rim, parallel to its sides.
    const double step = 1.0 - smooth(0.2, 0.55, std::abs(top.edge - 1.8));
    const double height = 6.0 * spec.variation * smooth(0.0, 1.2, top.edge) + spec.variation * rough;
    const double tone = 1.0 + 4.0 * spec.variation * (unit(top.id) - 0.5) + 2.0 * spec.variation * rough - 1.2 * spec.variation * step;
    return {height, mixed(scaled(spec.primary, tone), spec.secondary, joint)};
}

Surface paintPolishedColumns(const QcTextureSpec &spec, QPointF point)
{
    const Column column = columnAt(spec.seed, point, 0.0);
    const double joint = 1.0 - smooth(0.2, 0.6, column.joint);
    const double sheen = 1.0 - smooth(0.0, 0.9, std::abs(column.across + 0.35));
    const double tone = 0.94 + 0.12 * unit(column.id) + 0.14 * sheen;
    return {0.6 * smooth(0.0, 1.0, column.joint), mixed(scaled(spec.primary, tone), spec.secondary, joint)};
}

Surface paintFlowBands(const QcTextureSpec &spec, QPointF point)
{
    // Bands wander across the tile and repeat three times down it; some are thin, some wide.
    const QPointF bent = warped(spec.seed + 1, point, 8.0, 3.2);
    const double wave = std::sin(2.0 * std::numbers::pi * 3.0 * bent.y() / size + 1.5 * noise(spec.seed + 2, point, 8.0));
    const double fine = std::sin(2.0 * std::numbers::pi * 8.0 * bent.y() / size);
    const double band = smooth(-0.5, 0.5, wave + 0.35 * fine);
    return {0.3 * band, mixed(spec.secondary, spec.primary, band)};
}

// A shard of glassy stone: its cell, the place of the blow that broke it (on the cell's rim) and
// the distance to that place.
struct Shard
{
    Cell cell;
    double fromBlow = 0.0;
};

Shard shardAt(quint32 seed, QPointF point)
{
    const Cell cell = cellAt(seed + 1, warped(seed + 2, point, 8.0, 1.2), {3, 3, 0.95, false});
    const double turn = 2.0 * std::numbers::pi * unit(cell.id >> 5);
    const QPointF blow = cell.centre + 4.5 * QPointF(std::cos(turn), std::sin(turn));
    return {cell, length(point - blow)};
}

// Glass breaks in shell-shaped ripples around the blow.
Surface glassyStone(const Shard &shard, const QColor &body, const QColor &glint)
{
    const double ripple = std::sin(shard.fromBlow * 1.25 + 3.0 * unit(shard.cell.id));
    const double tone = 0.76 + 0.5 * unit(shard.cell.id >> 9) + 0.2 * ripple;

    // A thin bright line runs where a shard's edge catches the light, a dark one in the fracture.
    const double fracture = 1.0 - smooth(0.12, 0.42, shard.cell.edge);
    const double crest = smooth(0.8, 0.99, ripple) * smooth(0.5, 1.8, shard.cell.edge);
    const double height = 1.1 * ripple + 1.6 * smooth(0.0, 1.5, shard.cell.edge) - fracture;
    const QColor stone = mixed(scaled(body, tone), glint, 0.6 * crest);
    return {height, mixed(stone, scaled(body, 0.4), fracture)};
}

Surface paintGlassyStone(const QcTextureSpec &spec, QPointF point)
{
    return glassyStone(shardAt(spec.seed, point), spec.primary, spec.secondary);
}

Surface paintPolishedGlass(const QcTextureSpec &spec, QPointF point)
{
    // Ground flat, the shards show as fields of their own tone in clouded stone, the fractures
    // as pale veins. Nothing runs in one direction, so tiled blocks show no stripes.
    const Shard shard = shardAt(spec.seed, point);
    const double cloud = fractal(spec.seed + 1, point, 4.0, 3);
    const double field = 0.1 + 0.55 * unit(shard.cell.id >> 9) + 0.5 * cloud;
    const double vein = 1.0 - smooth(0.05, 0.3, shard.cell.edge);
    return {0.0, mixed(mixed(spec.primary, spec.secondary, field), scaled(spec.secondary, 1.5), 0.75 * vein)};
}

Surface paintGildedStone(const QcTextureSpec &spec, QPointF point)
{
    const Shard shard = shardAt(spec.seed, point);
    const Surface stone = glassyStone(shard, spec.primary, scaled(spec.primary, 2.6));

    // Gold fills the fractures in parts of the tile, wider in some places.
    const double width = 0.75 + 0.5 * fractal(spec.seed + 3, point, 4.0, 2);
    const double region = smooth(-0.1, 0.1, fractal(spec.seed + 5, point, 8.0, 1));
    const double seam = region * (1.0 - smooth(std::max(width, 0.25) * 0.55, std::max(width, 0.25), shard.cell.edge));
    const double shine = 0.85 + 0.35 * fractal(spec.seed + 4, point, 2.0, 2);
    return {stone.height * (1.0 - seam) + 1.4 * seam, mixed(stone.colour, scaled(spec.secondary, shine), seam)};
}

constexpr std::array<Entry, 7> surfaces = {{
    {Pattern::ColumnSide, paintColumnSide},
    {Pattern::ColumnTop, paintColumnTop},
    {Pattern::PolishedColumns, paintPolishedColumns},
    {Pattern::FlowBands, paintFlowBands},
    {Pattern::GlassyStone, paintGlassyStone},
    {Pattern::PolishedGlass, paintPolishedGlass},
    {Pattern::GildedStone, paintGildedStone},
}};

} // namespace

std::span<const Entry> qcStoneSurfaces()
{
    return surfaces;
}
