// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcIntMath.h"
#include "common/core/QcWorldConstants.h"

#include <QTypeInfo>

#include <compare>

// Integer position of a block in the world, in blocks (= metres).
struct QcBlockPos
{
    int x = 0;
    int y = 0;
    int z = 0;

    auto operator<=>(const QcBlockPos &) const = default;

    QcChunkPos chunk() const { return {qcFloorDiv(x, QcWorld::chunkSize), qcFloorDiv(z, QcWorld::chunkSize)}; }
    int localX() const { return qcFloorMod(x, QcWorld::chunkSize); }
    int localZ() const { return qcFloorMod(z, QcWorld::chunkSize); }
    bool isInsideWorldHeight() const { return y >= 0 && y < QcWorld::worldHeight; }
    QcBlockPos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
};

Q_DECLARE_TYPEINFO(QcBlockPos, Q_PRIMITIVE_TYPE);

size_t qHash(QcBlockPos pos, size_t seed = 0) noexcept;
