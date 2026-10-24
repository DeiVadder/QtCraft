#include "clientcore/meshing/QcTetherMesher.h"

#include <array>
#include <cmath>

namespace QcTetherMesher {

namespace {

QcVec3 cross(const QcVec3 &a, const QcVec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// A flat ribbon from `from` to `to`, `side` wide on each side.
void appendRibbon(QcEntityMesh &mesh, const Thread &thread, const QcVec3 &side, const QRectF &uv, QcEntityLight light)
{
    const std::array<QcVec3, 4> corners{thread.from - side, thread.to - side, thread.to + side, thread.from + side};
    std::array<QcEntityVertex, 4> quad;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        QcEntityVertex &vertex = quad[i];
        vertex = mesh.vertexAt(corners[i]);
        vertex.u = static_cast<float>(i == 0 || i == 3 ? uv.left() : uv.right());
        vertex.v = static_cast<float>(i < 2 ? uv.bottom() : uv.top());
        vertex.skyLight = light.sky;
        vertex.blockLight = light.block;
    }
    mesh.addQuad(quad, true);
}

} // namespace

void append(QcEntityMesh &mesh, const Thread &thread, const QRectF &uv, QcEntityLight light)
{
    const QcVec3 along = thread.to - thread.from;
    if (along.length() < 1e-6) {
        return;
    }

    // Two ribbons across each other: one lying across the thread and up, one across both.
    const QcVec3 direction = along.normalized();
    const QcVec3 helper = std::abs(direction.y) < 0.9 ? QcVec3{0.0, 1.0, 0.0} : QcVec3{1.0, 0.0, 0.0};
    const QcVec3 first = cross(direction, helper).normalized();
    const QcVec3 second = cross(direction, first).normalized();
    appendRibbon(mesh, thread, first * (thickness * 0.5), uv, light);
    appendRibbon(mesh, thread, second * (thickness * 0.5), uv, light);
}

} // namespace QcTetherMesher
