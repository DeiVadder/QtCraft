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

QVector4D entry(const QcVec3 &origin, const QcVec3 &camera, float cell)
{
    const QcVec3 offset = origin - camera;
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
