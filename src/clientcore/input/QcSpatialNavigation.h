// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QRectF>

#include <optional>

// Moving a focus between rectangles of a screen by direction (decision E-583): from the focused
// rectangle to the nearest one that lies in the direction. Pure geometry, for the pad's focus in
// every screen (QcMenuNavigator); a grid of slots, a column of buttons and a list with rows of
// different widths all move to their visual neighbour.
namespace QcSpatialNavigation {

enum class Direction { Up, Down, Left, Right };

// The candidate to move to from `from` in `direction`, or nullopt when nothing lies that way. A
// candidate counts when its centre lies beyond the centre of `from` in that direction, further than
// it lies off to the side (its edge's distance across the direction); the cost is
// the gap along the direction plus twice the offset across it (zero while the two overlap across
// it), so the neighbour in the same row or column wins over a nearer one diagonally.
std::optional<qsizetype> next(const QRectF &from, Direction direction, const QList<QRectF> &candidates);

// Where the focus starts without one: the top row first, in it the leftmost (rows by centre, with
// a tolerance of half the smaller height).
std::optional<qsizetype> first(const QList<QRectF> &candidates);

} // namespace QcSpatialNavigation
