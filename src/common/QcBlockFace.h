#pragma once

#include "QcBlockPos.h"

#include <array>

// Six axis-aligned block faces. The order is part of the content format (face texture arrays).
enum class QcBlockFace : quint8 { East, West, Up, Down, South, North };

inline constexpr int qcBlockFaceCount = 6;

inline constexpr std::array<QcBlockFace, qcBlockFaceCount> qcAllBlockFaces = {
    QcBlockFace::East, QcBlockFace::West, QcBlockFace::Up, QcBlockFace::Down, QcBlockFace::South, QcBlockFace::North};

struct QcFaceOffset
{
    int dx = 0;
    int dy = 0;
    int dz = 0;
};

// East = +X, Up = +Y, South = +Z.
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

constexpr QcBlockPos qcNeighbour(QcBlockPos pos, QcBlockFace face)
{
    const QcFaceOffset offset = qcFaceOffset(face);
    return {pos.x + offset.dx, pos.y + offset.dy, pos.z + offset.dz};
}
