// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcRenderOrigin.h"

#include "common/core/QcIntMath.h"

#include <cmath>

namespace QcRenderOrigin {

namespace {

// Sixteens of a block of a coordinate modulo hashPeriod.
int cellOf(int coordinate)
{
    return qcFloorMod(coordinate, hashPeriod) / QcWorld::chunkSize;
}

} // namespace

bool outOfReach(const QcVec3 &anchor, const QcVec3 &camera)
{
    const QcVec3 offset = camera - anchor;
    return std::abs(offset.x) > anchorReach || std::abs(offset.y) > anchorReach || std::abs(offset.z) > anchorReach;
}

QVector4D entry(const QcVec3 &origin, const QcVec3 &anchor, float cell)
{
    const QcVec3 offset = origin - anchor;
    return {static_cast<float>(offset.x), static_cast<float>(offset.y), static_cast<float>(offset.z), cell};
}

float sectionCell(QcBlockPos corner)
{
    constexpr int cells = hashPeriod / QcWorld::chunkSize;
    return static_cast<float>(cellOf(corner.x) + cells * cellOf(corner.y) + cells * cells * cellOf(corner.z));
}

QcVec3 blockNear(const QcVec3 &position)
{
    return {std::floor(position.x), std::floor(position.y), std::floor(position.z)};
}

} // namespace QcRenderOrigin
