#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QRandomGenerator>

#include <optional>

// Safe places for bodies that teleport (decision E-460): an enderman's jump and the thrower of an
// ender pearl. Only blocks of loaded chunks count (a cell of a chunk that is not loaded is never
// safe), so a teleport crosses a chunk border only into a loaded chunk.
namespace QcSafeSpot {

// The cells a body of `height` metres takes up.
int cellsOf(double height);
// Whether a body fits with its feet in `feet`: its `cells` cells are open (not solid), hold no liquid
// and hurt nothing (fire, magma), all loaded. No floor is needed.
bool fits(const QcBlockAccess &world, QcBlockPos feet, int cells);
// Whether it fits there (fits) and stands on a floor: the cell below is a full solid block of a
// loaded chunk that is no liquid and does not hurt.
bool canStand(const QcBlockAccess &world, QcBlockPos feet, int cells);

// Where to look for a place to stand around a point.
struct Search
{
    // Horizontal and vertical distance from the point, in metres.
    double range = 32.0;
    int attempts = 16;
    // The cells of the body (cellsOf).
    int cells = 2;
};

// A random place to stand near `around` (the middle of the cell, the feet on the floor): each
// attempt picks a random cell within the search's range and goes down from there to the first floor
// (at most twice the range); the first that canStand wins. None when no attempt finds one.
std::optional<QcVec3> randomAround(const QcBlockAccess &world, const QcVec3 &around, Search search, QRandomGenerator &random);
// The place nearest `point` where a body of `cells` fits, with the feet in the middle of the bottom of
// a cell (no wall reaches into the body), reached from the side it came from along `along` (a flight
// direction; zero for none): the open cell it came through (the point's own cell when that is open,
// else the first open one back along `along` within `backtrack` metres) must be one of the body's
// cells, the feet in it first, then lower; else a cell beside it that shares an open face with it.
// So a pearl never brings its thrower through a wall, a floor or a roof one block thick. None when
// nothing near fits.
inline constexpr double backtrack = 2.0;
std::optional<QcVec3> nearest(const QcBlockAccess &world, const QcVec3 &point, const QcVec3 &along, int cells);

} // namespace QcSafeSpot
