// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QRandomGenerator>

#include <optional>

/*! \brief Safe places for bodies that teleport (decision E-460): a shadewalker's jump and the
    thrower of a shade pearl.

    Only blocks of loaded chunks count (a cell of a chunk that is not loaded is never safe), so a
    teleport crosses a chunk border only into a loaded chunk. */
namespace QcSafeSpot {

/*! Returns the cells a body of \a height metres takes up. */
int cellsOf(double height);
/*! Returns whether a body fits in \a world with its feet in \a feet: its \a cells cells are open
    (not solid), hold no liquid and hurt nothing (fire, magma), all loaded. No floor is needed. */
bool fits(const QcBlockAccess &world, QcBlockPos feet, int cells);
/*! Returns whether a body of \a cells cells fits in \a world with its feet in \a feet (fits()) and
    stands on a floor: the cell below is a full solid block of a loaded chunk that is no liquid and
    does not hurt. */
bool canStand(const QcBlockAccess &world, QcBlockPos feet, int cells);

/*! \brief Where to look for a place to stand around a point. */
struct Search
{
    /*! Horizontal and vertical distance from the point, in metres. */
    double range = 32.0;
    /*! Random cells tried. */
    int attempts = 16;
    /*! The cells of the body (cellsOf). */
    int cells = 2;
};

/*! Returns a random place in \a world to stand near \a around, drawn from \a random (the middle
    of the cell, the feet on the floor): each attempt of \a search picks a random cell within its
    range and goes down from there to the first floor (at most twice the range); the first that
    canStand wins. None when no attempt finds one. */
std::optional<QcVec3> randomAround(const QcBlockAccess &world, const QcVec3 &around, Search search, QRandomGenerator &random);
/*! How far back along the flight direction nearest() looks for the open cell a body came
    through, in metres. */
inline constexpr double backtrack = 2.0;
/*! Returns the place of \a world nearest \a point where a body of \a cells fits, with the feet in
    the middle of the bottom of a cell (no wall reaches into the body), reached from the side it
    came from along \a along (a flight direction; zero for none): the open cell it came through (the
    point's own cell when that is open, else the first open one back along \a along within backtrack
    metres) must be one of the body's cells, the feet in it first, then lower; else a cell beside it
    that shares an open face with it. So a pearl never brings its thrower through a wall, a floor or
    a roof one block thick. None when nothing near fits. */
std::optional<QcVec3> nearest(const QcBlockAccess &world, const QcVec3 &point, const QcVec3 &along, int cells);
/*! Returns the place of \a world nearest \a point within \a reach cells to the side where a body
    of \a cells can stand (canStand), the feet in the middle of the cell, at most three cells higher
    or lower. Cells right beside the point come first, then its own cell, then those further out, so
    a body in a closed room stays in it. None when there is no floor within reach (a lava sea). */
std::optional<QcVec3> standBeside(const QcBlockAccess &world, const QcVec3 &point, int reach, int cells);

} // namespace QcSafeSpot
