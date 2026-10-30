// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <array>

/*! \brief Six axis-aligned block faces.

    The order is part of the content format (face texture arrays). */
enum class QcBlockFace : quint8 {
    /*! +X */
    East,
    /*! -X */
    West,
    /*! +Y */
    Up,
    /*! -Y */
    Down,
    /*! +Z */
    South,
    /*! -Z */
    North
};

/*! The number of faces. */
inline constexpr int qcBlockFaceCount = 6;

/*! All faces in the order of the enum. */
inline constexpr std::array<QcBlockFace, qcBlockFaceCount> qcAllBlockFaces = {
    QcBlockFace::East, QcBlockFace::West, QcBlockFace::Up, QcBlockFace::Down, QcBlockFace::South, QcBlockFace::North};

/*! \brief The step from a block to its neighbour across one face, in blocks. \qcfields */
struct QcFaceOffset
{
    int dx = 0;
    int dy = 0;
    int dz = 0;
};

/*! Returns the step across \a face: East = +X, Up = +Y, South = +Z. */
constexpr QcFaceOffset qcFaceOffset(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return {1, 0, 0};
    case QcBlockFace::West:
        return {-1, 0, 0};
    case QcBlockFace::Up:
        return {0, 1, 0};
    case QcBlockFace::Down:
        return {0, -1, 0};
    case QcBlockFace::South:
        return {0, 0, 1};
    case QcBlockFace::North:
        return {0, 0, -1};
    }
    return {};
}

/*! Returns the block next to \a pos across \a face. */
constexpr QcBlockPos qcNeighbour(QcBlockPos pos, QcBlockFace face)
{
    const QcFaceOffset offset = qcFaceOffset(face);
    return {pos.x + offset.dx, pos.y + offset.dy, pos.z + offset.dz};
}
