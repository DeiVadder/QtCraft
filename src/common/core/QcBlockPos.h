// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcIntMath.h"
#include "common/core/QcWorldConstants.h"

#include <QTypeInfo>

#include <compare>

/*! \brief Integer position of a block in the world, in blocks (= metres). */
struct QcBlockPos
{
    /*! East (+) and west (-). */
    int x = 0;
    /*! Height. */
    int y = 0;
    /*! South (+) and north (-). */
    int z = 0;

    auto operator<=>(const QcBlockPos &) const = default;

    /*! Returns the chunk column that holds the block. */
    QcChunkPos chunk() const { return {qcFloorDiv(x, QcWorld::chunkSize), qcFloorDiv(z, QcWorld::chunkSize)}; }
    /*! Returns x within the chunk (0..QcWorld::chunkSize - 1). */
    int localX() const { return qcFloorMod(x, QcWorld::chunkSize); }
    /*! Returns z within the chunk (0..QcWorld::chunkSize - 1). */
    int localZ() const { return qcFloorMod(z, QcWorld::chunkSize); }
    /*! Returns whether y lies within the world's height (0..QcWorld::worldHeight - 1). */
    bool isInsideWorldHeight() const { return y >= 0 && y < QcWorld::worldHeight; }
    /*! Returns the position moved by \a dx, \a dy and \a dz. */
    QcBlockPos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
};

Q_DECLARE_TYPEINFO(QcBlockPos, Q_PRIMITIVE_TYPE);

/*! Returns the hash of \a pos for QHash, mixed with \a seed. */
size_t qHash(QcBlockPos pos, size_t seed = 0) noexcept;
