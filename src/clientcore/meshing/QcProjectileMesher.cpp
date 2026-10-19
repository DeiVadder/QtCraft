#include "clientcore/meshing/QcProjectileMesher.h"

#include <QtMath>

#include <array>
#include <cmath>

namespace {

// One double-sided square: its corners at the tail, across, head and the other side across.
void emitSquare(QcEntityMesh &mesh, const std::array<QcVec3, 4> &corners, const QRectF &icon, QcEntityLight light)
{
    // Tail: bottom left of the icon; head: top right; `across` takes the top left.
    const std::array<std::pair<float, float>, 4> uvs = {{{float(icon.left()), float(icon.bottom())},
                                                         {float(icon.left()), float(icon.top())},
                                                         {float(icon.right()), float(icon.top())},
                                                         {float(icon.right()), float(icon.bottom())}}};
    std::array<QcEntityVertex, 4> quad;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        QcEntityVertex vertex;
        vertex.x = float(corners[i].x);
        vertex.y = float(corners[i].y);
        vertex.z = float(corners[i].z);
        vertex.u = uvs[i].first;
        vertex.v = uvs[i].second;
        vertex.shade = 0.95f;
        vertex.skyLight = light.sky;
        vertex.blockLight = light.block;
        quad[i] = vertex;
    }
    mesh.addQuad(quad, true);
}

} // namespace

namespace QcProjectileMesher {

void append(QcEntityMesh &mesh, const Placement &placement, const QRectF &icon)
{
    const double y = qDegreesToRadians(placement.yaw);
    const double p = qDegreesToRadians(placement.pitch);
    const QcVec3 forward{std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    const QcVec3 right{std::cos(y), 0.0, std::sin(y)};
    const QcVec3 up{-std::sin(y) * std::sin(p), std::cos(p), std::cos(y) * std::sin(p)};

    const double half = length * 0.5;
    const QcVec3 tail = placement.centre - forward * half;
    const QcVec3 head = placement.centre + forward * half;
    for (const QcVec3 &across : {up, right}) {
        emitSquare(mesh, {tail, placement.centre + across * half, head, placement.centre - across * half}, icon, placement.light);
    }
}

} // namespace QcProjectileMesher
