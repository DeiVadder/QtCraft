// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureMasonryPatterns.h"

#include "clientcore/textures/QcTextureField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace {

using namespace QcTextureField;
using Pattern = QcTextureSpec::Pattern;
using QcTexturePaint::hashPixel;
using QcTexturePaint::scaled;
using QcTexturePaint::size;
using QcTextureRelief::Entry;
using QcTextureRelief::Surface;

// The stone around a point: the way to the nearest joint and an id for its tone. Joints lie in
// the middle of a pixel, so at 16 x 16 they are one pixel wide.
struct Stone
{
    double edge = 0.0;
    quint32 id = 0;
};

double apart(double first, double second)
{
    return std::abs(std::remainder(first - second, static_cast<double>(size)));
}

// The rows of the joints between the courses, in order; a course lies above each.
constexpr std::array<double, 3> ashlarCourses{5.5, 10.5, 15.5};
constexpr std::array<double, 4> brickCourses{3.5, 7.5, 11.5, 15.5};

// The stone around `point` in `courses`: each course has two joints, so a longer and a shorter
// stone, shifted from course to course by the seed.
Stone stoneAt(quint32 seed, std::span<const double> courses, QPointF point)
{
    // Rows below the last joint belong to the first course of the next tile.
    const double y = point.y() > courses.back() ? point.y() - size : point.y();
    std::size_t course = 0;
    while (course + 1 < courses.size() && y > courses[course]) {
        ++course;
    }
    const double above = course == 0 ? courses.back() - size : courses[course - 1];
    const double below = courses[course];
    const quint32 hash = hashPixel(seed, static_cast<int>(course), 0);
    const double first = std::floor(16.0 * unit(hash)) + 0.5;
    const double second = first + 6.0 + std::floor(4.0 * unit(hash >> 16));
    const double x = std::fmod(point.x() - first + 2.0 * size, static_cast<double>(size));
    const quint32 side = x < second - first ? 0u : 1u;
    const double across = std::min(apart(point.x(), first), apart(point.x(), second));
    return {std::min({y - above, below - y, across}), hashPixel(seed + 1, static_cast<int>(course), static_cast<int>(side))};
}

// How open a crack is at `point`: fine crooked lines in parts of the tile.
double crackAt(quint32 seed, QPointF point)
{
    const Cell cell = cellAt(seed + 5, warped(seed + 6, point, 4.0, 1.4), {2, 2, 0.9, false});
    const double region = smooth(-0.15, 0.1, noise(seed + 7, point, 8.0));
    return region * (1.0 - smooth(0.2, 0.55, cell.edge));
}

Surface ashlar(const QcTextureSpec &spec, QPointF point)
{
    const Stone stone = stoneAt(spec.seed, ashlarCourses, point);
    const double joint = 1.0 - smooth(0.42, 0.62, stone.edge);
    const double chamfer = smooth(0.4, 1.3, stone.edge);
    const double cloud = fractal(spec.seed + 2, point, 4.0, 3);
    const QColor face = scaled(spec.primary, 0.9 + 0.26 * unit(stone.id) + 0.22 * cloud);
    return {2.6 * chamfer, mixed(face, spec.secondary, joint)};
}

Surface withCracks(const QcTextureSpec &spec, const Surface &surface, QPointF point)
{
    const double crack = crackAt(spec.seed, point);
    return {surface.height - 1.4 * crack, mixed(surface.colour, scaled(spec.secondary, 0.8), crack)};
}

Surface paintCrackedAshlar(const QcTextureSpec &spec, QPointF point)
{
    return withCracks(spec, ashlar(spec, point), point);
}

// The way to the frame of the panel: 0 at the tile's edge.
double panelDepth(QPointF point)
{
    const double middle = size / 2.0;
    return middle - std::max(std::abs(point.x() - middle), std::abs(point.y() - middle));
}

Surface paintCarvedAshlar(const QcTextureSpec &spec, QPointF point)
{
    // A raised frame, a sunken panel, in it a stepped diamond with a boss.
    const double depth = panelDepth(point);
    const double frame = smooth(0.4, 0.9, depth) * (1.0 - smooth(2.3, 2.8, depth));
    const double middle = size / 2.0;
    const double diamond = std::abs(point.x() - middle) + std::abs(point.y() - middle);
    const double steps = std::floor(std::max(0.0, 7.0 - diamond) / 2.0);
    const double height = 2.2 * frame + (depth > 2.8 ? 0.8 * steps : 0.0);
    const double cloud = fractal(spec.seed + 2, point, 4.0, 3);
    const double joint = 1.0 - smooth(0.42, 0.62, depth);
    const double panel = depth > 2.8 && diamond >= 7.0 ? 0.62 : 0.92 + 0.08 * steps;
    return {height, mixed(scaled(spec.primary, panel + 0.2 * cloud), spec.secondary, joint)};
}

Surface firedBricks(const QcTextureSpec &spec, QPointF point)
{
    const Stone brick = stoneAt(spec.seed, brickCourses, point);
    const double mortar = 1.0 - smooth(0.4, 0.62, brick.edge);
    const double scorch = smooth(0.05, 0.5, fractal(spec.seed + 2, point, 8.0, 2));
    const double rough = fractal(spec.seed + 3, point, 2.0, 3);
    const double tone = 0.88 + 0.3 * unit(brick.id) + 0.24 * rough;
    const QColor face = mixed(scaled(spec.primary, tone), spec.secondary, 0.6 * scorch);
    return {2.2 * smooth(0.4, 1.5, brick.edge) + 0.5 * rough, mixed(face, scaled(spec.secondary, 0.7), mortar)};
}

Surface paintCrackedFiredBricks(const QcTextureSpec &spec, QPointF point)
{
    return withCracks(spec, firedBricks(spec, point), point);
}

Surface paintCarvedFiredBricks(const QcTextureSpec &spec, QPointF point)
{
    // A frame of one course, in it two chevrons pointing up.
    const double depth = panelDepth(point);
    const double frame = 1.0 - smooth(0.4, 0.62, std::abs(depth - 3.5));
    const double middle = size / 2.0;
    const double slope = std::abs(point.x() - middle);
    const double chevron = std::remainder(point.y() + slope - 1.5, 5.0);
    const double carved = depth > 4.0 && std::abs(chevron) < 1.1 ? 1.0 : 0.0;
    const double rough = fractal(spec.seed + 3, point, 2.0, 3);
    const double height = 2.0 * smooth(0.3, 1.0, depth) - 1.8 * frame + 1.6 * carved + 0.4 * rough;
    const double edge = 1.0 - smooth(0.4, 0.62, depth);
    const QColor face = scaled(spec.primary, 0.94 + 0.34 * carved + 0.28 * rough);
    return {height, mixed(face, scaled(spec.secondary, 0.7), std::max(edge, frame))};
}

constexpr std::array<Entry, 6> surfaces = {{
    {Pattern::Ashlar, ashlar},
    {Pattern::CrackedAshlar, paintCrackedAshlar},
    {Pattern::CarvedAshlar, paintCarvedAshlar},
    {Pattern::FiredBricks, firedBricks},
    {Pattern::CrackedFiredBricks, paintCrackedFiredBricks},
    {Pattern::CarvedFiredBricks, paintCarvedFiredBricks},
}};

} // namespace

std::span<const Entry> qcMasonrySurfaces()
{
    return surfaces;
}
