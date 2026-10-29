// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcFrustum.h"

namespace QcFrustum {

Planes planes(const QMatrix4x4 &viewProjection)
{
    // A point p is inside when -w <= x <= w, -w <= y <= w, z <= w and w > 0 in clip space.
    const QVector4D x = viewProjection.row(0);
    const QVector4D y = viewProjection.row(1);
    const QVector4D z = viewProjection.row(2);
    const QVector4D w = viewProjection.row(3);
    return {w + x, w - x, w + y, w - y, w - z, w};
}

bool intersectsBox(const Planes &planes, QVector3D boxMin, QVector3D boxMax)
{
    for (const QVector4D &plane : planes) {
        const float furthest = plane.x() * (plane.x() >= 0.0f ? boxMax.x() : boxMin.x())
                               + plane.y() * (plane.y() >= 0.0f ? boxMax.y() : boxMin.y())
                               + plane.z() * (plane.z() >= 0.0f ? boxMax.z() : boxMin.z()) + plane.w();
        if (furthest < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace QcFrustum
