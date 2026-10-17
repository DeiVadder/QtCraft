#pragma once

#include "common/core/QcBlockFace.h"

// How light passes block states whose shape is not a full cube (decision E-104): such a state of a
// light-dimming block lets light into its cell (opacity 0) but not across the faces its shape covers
// completely. A bottom slab is lit from above and the sides but keeps the sky from the cell below; a
// stair passes light through its open quarter. Faces are bits per QcBlockFace.
namespace QcLightPassage {

constexpr quint8 bit(QcBlockFace face)
{
    return quint8(1u << unsigned(face));
}

constexpr QcBlockFace opposite(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return QcBlockFace::West;
    case QcBlockFace::West:
        return QcBlockFace::East;
    case QcBlockFace::Up:
        return QcBlockFace::Down;
    case QcBlockFace::Down:
        return QcBlockFace::Up;
    case QcBlockFace::South:
        return QcBlockFace::North;
    case QcBlockFace::North:
        return QcBlockFace::South;
    }
    return face;
}

// Whether light leaves a cell whose state blocks the faces `fromBlocked` across `face` into the
// neighbour whose state blocks `toBlocked`.
constexpr bool crosses(quint8 fromBlocked, quint8 toBlocked, QcBlockFace face)
{
    return (fromBlocked & bit(face)) == 0 && (toBlocked & bit(opposite(face))) == 0;
}

// What a cell does to sky light coming straight down: nothing, keeps it out of the cell itself
// (dimming full cubes, covered top face), or lets it in but not further down (covered bottom face).
enum class SkyStop { None, Cell, Below };

constexpr SkyStop skyStop(int opacity, quint8 blockedFaces)
{
    if (opacity > 0 || (blockedFaces & bit(QcBlockFace::Up)) != 0) {
        return SkyStop::Cell;
    }
    return (blockedFaces & bit(QcBlockFace::Down)) != 0 ? SkyStop::Below : SkyStop::None;
}

// Highest cell of a column that sky light coming straight down does not reach (cells above it are
// fully lit) when the cell `y` stops it as `stop`; -1 (keep looking further down) for SkyStop::None.
constexpr int skyTop(SkyStop stop, int y)
{
    switch (stop) {
    case SkyStop::Cell:
        return y;
    case SkyStop::Below:
        return y - 1;
    case SkyStop::None:
        return -1;
    }
    return -1;
}

} // namespace QcLightPassage
