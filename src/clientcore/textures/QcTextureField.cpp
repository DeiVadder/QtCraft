// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureField.h"

#include "clientcore/textures/QcTexturePaint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace QcTextureField {

namespace {

using QcTexturePaint::hashPixel;
using QcTexturePaint::size;

// How many cells around the point's own cell are looked at; two reach every neighbour of a
// jittered or staggered grid.
constexpr int reach = 2;
constexpr std::size_t searchedSide = 2 * reach + 1;
constexpr std::size_t searched = searchedSide * searchedSide;

// `value` wrapped into 0..period - 1, also for negative values.
int wrapped(int value, int period)
{
    return ((value % period) + period) % period;
}

double dot(QPointF first, QPointF second)
{
    return first.x() * second.x() + first.y() * second.y();
}

struct Site
{
    QPointF centre;
    quint32 id = 0;
};

// The centre of cell (column, row), which may lie outside the grid: the cell repeats there.
Site siteAt(quint32 seed, const Grid &grid, int column, int row)
{
    const quint32 id = hashPixel(seed, wrapped(column, grid.columns), wrapped(row, grid.rows));
    const double width = static_cast<double>(size) / grid.columns;
    const double height = static_cast<double>(size) / grid.rows;
    const double stagger = grid.staggered && wrapped(row, 2) == 1 ? 0.5 : 0.0;
    const double dx = grid.jitter * (unit(id) - 0.5);
    const double dy = grid.jitter * (unit(id >> 16) - 0.5);
    return {{(column + 0.5 + stagger + dx) * width, (row + 0.5 + dy) * height}, id};
}

std::array<Site, searched> sitesAround(quint32 seed, QPointF point, const Grid &grid)
{
    const int column = static_cast<int>(std::floor(point.x() * grid.columns / size));
    const int row = static_cast<int>(std::floor(point.y() * grid.rows / size));
    std::array<Site, searched> sites{};
    std::size_t next = 0;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            sites[next] = siteAt(seed, grid, column + dx, row + dy);
            ++next;
        }
    }
    return sites;
}

} // namespace

double smooth(double from, double to, double value)
{
    const double share = std::clamp((value - from) / (to - from), 0.0, 1.0);
    return share * share * (3.0 - 2.0 * share);
}

QColor mixed(const QColor &first, const QColor &second, double share)
{
    const double t = std::clamp(share, 0.0, 1.0);
    const auto channel = [t](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
    return QColor(channel(first.red(), second.red()), channel(first.green(), second.green()), channel(first.blue(), second.blue()));
}

double unit(quint32 id)
{
    return static_cast<double>(id & 0xFFFFu) / 65535.0;
}

double length(QPointF vector)
{
    return std::sqrt(dot(vector, vector));
}

QPointF wrappedOffset(QPointF from, QPointF to)
{
    return {std::remainder(to.x() - from.x(), static_cast<double>(size)), std::remainder(to.y() - from.y(), static_cast<double>(size))};
}

QPointF scattered(quint32 seed, int index)
{
    // A low-discrepancy sequence (steps of the plastic number) spreads the places evenly without
    // a grid; the seed shifts it and each place strays a little.
    const quint32 id = hashPixel(seed, index, 0);
    const quint32 shift = hashPixel(seed, -1, 0);
    const double across = unit(shift) + index * 0.7548776662 + 0.14 * (unit(id) - 0.5);
    const double down = unit(shift >> 16) + index * 0.5698402910 + 0.14 * (unit(id >> 16) - 0.5);
    return {size * (across - std::floor(across)), size * (down - std::floor(down))};
}

double noise(quint32 seed, QPointF point, double pitch)
{
    return noise(seed, point, QSizeF(pitch, pitch));
}

double noise(quint32 seed, QPointF point, QSizeF pitch)
{
    const int across = std::max(1, static_cast<int>(std::lround(size / pitch.width())));
    const int down = std::max(1, static_cast<int>(std::lround(size / pitch.height())));
    const double x = point.x() * across / size;
    const double y = point.y() * down / size;
    const int left = static_cast<int>(std::floor(x));
    const int top = static_cast<int>(std::floor(y));
    const double rightShare = smooth(0.0, 1.0, x - left);
    const double lowerShare = smooth(0.0, 1.0, y - top);
    const auto corner = [&](int dx, int dy) { return QcTexturePaint::noise(seed, wrapped(left + dx, across), wrapped(top + dy, down)); };
    const double upper = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * rightShare;
    const double lower = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * rightShare;
    return upper + (lower - upper) * lowerShare;
}

double fractal(quint32 seed, QPointF point, double pitch, int octaves)
{
    double sum = 0.0;
    double strength = 0.6;
    double step = pitch;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += strength * noise(seed + static_cast<quint32>(octave) * 101u, point, step);
        strength *= 0.5;
        step *= 0.5;
    }
    return sum;
}

QPointF warped(quint32 seed, QPointF point, double pitch, double amount)
{
    return point + QPointF(amount * fractal(seed, point, pitch, 2), amount * fractal(seed + 57u, point, pitch, 2));
}

Cell cellAt(quint32 seed, QPointF point, const Grid &grid)
{
    const std::array<Site, searched> sites = sitesAround(seed, point, grid);
    const Site *nearest = &sites.front();
    double nearestDistance = std::numeric_limits<double>::max();
    for (const Site &site : sites) {
        const double distance = length(site.centre - point);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = &site;
        }
    }

    // The way to each border is measured along the line between the two centres.
    double edge = std::numeric_limits<double>::max();
    for (const Site &site : sites) {
        const QPointF between = site.centre - nearest->centre;
        const double apart = length(between);
        if (apart > 1e-9) {
            const QPointF middle = (site.centre + nearest->centre) / 2.0;
            edge = std::min(edge, dot(middle - point, between / apart));
        }
    }
    return {nearest->centre, nearestDistance, edge, nearest->id};
}

} // namespace QcTextureField
