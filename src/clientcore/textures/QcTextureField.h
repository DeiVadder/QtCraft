// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QtGlobal>

/*! \brief Fields over a texture's tile for the relief patterns (decision E-1043): smooth noise and
    irregular cells. Points are in pixels of the 16 x 16 tile, but not bound to whole pixels, so
    the same field can be read at any resolution; every field repeats with the tile. */
namespace QcTextureField {

/*! Returns for \a value 0 below \a from, 1 above \a to and a smooth curve between them;
    \a from < \a to. */
[[nodiscard]] double smooth(double from, double to, double value);

/*! Returns \a first mixed towards \a second by \a share (0..1); alpha is opaque. */
[[nodiscard]] QColor mixed(const QColor &first, const QColor &second, double share);

/*! Returns \a id as a value in 0..1. */
[[nodiscard]] double unit(quint32 id);

/*! Returns the length of \a vector. */
[[nodiscard]] double length(QPointF vector);

/*! Returns the way from \a from to \a to, the short way round the repeating tile. */
[[nodiscard]] QPointF wrappedOffset(QPointF from, QPointF to);

/*! Returns a place on the tile for number \a index of a scattered feature of \a seed; the places
    spread over the tile evenly but on no grid, and they do not need to stay inside a cell. */
[[nodiscard]] QPointF scattered(quint32 seed, int index);

/*! Returns smooth noise in -1..1 at \a point, with features \a pitch pixels apart across and down;
    the tile holds a whole number of them, at least two (pitch 8 or less): with one, the noise
    is a constant. \a seed chooses the field. */
[[nodiscard]] double noise(quint32 seed, QPointF point, double pitch);

/*! Returns the noise of \a seed at \a point with its own pitch across and down, the width and
    height of \a pitch, for streaks. */
[[nodiscard]] double noise(quint32 seed, QPointF point, QSizeF pitch);

/*! Returns noise of \a seed at \a point summed over \a octaves, the first with \a pitch, each
    further one half the pitch and half the strength of the one before, in about -1..1. */
[[nodiscard]] double fractal(quint32 seed, QPointF point, double pitch, int octaves);

/*! Returns \a point pushed up to \a amount pixels by noise of \a seed and \a pitch; lines drawn
    through the result wander instead of running straight. */
[[nodiscard]] QPointF warped(quint32 seed, QPointF point, double pitch, double amount);

/*! \brief How the tile is divided into cells. \qcfields columns and rows are the number of cells
    across and down; jitter (0..1) moves each cell's centre off the grid by that share of a cell;
    staggered shifts every other row by half a cell, for hexagons (rows must be even then). */
struct Grid
{
    int columns = 4;
    int rows = 4;
    double jitter = 0.8;
    bool staggered = false;
};

/*! \brief The cell around a point. \qcfields centre is the cell's centre, near the point (it
    may lie beyond the tile); distance is the way from the point to it; edge is the way to the
    nearest border of the cell; id is the same for all points of the cell and differs by seed. */
struct Cell
{
    QPointF centre;
    double distance = 0.0;
    double edge = 0.0;
    quint32 id = 0;
};

/*! Returns the cell around \a point in the cells of \a grid placed by \a seed (a Voronoi diagram
    with straight borders; warp the point for crooked ones). */
[[nodiscard]] Cell cellAt(quint32 seed, QPointF point, const Grid &grid);

} // namespace QcTextureField
