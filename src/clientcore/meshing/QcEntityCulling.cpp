#include "clientcore/meshing/QcEntityCulling.h"

#include "clientcore/session/QcClientEntity.h"

#include <algorithm>

namespace QcEntityCulling {

namespace {

QVector3D toVector(const QcVec3 &v)
{
    return {float(v.x), float(v.y), float(v.z)};
}

QcAabb grown(const QcAabb &box, double by)
{
    const QcVec3 delta{by, by, by};
    return {box.min - delta, box.max + delta};
}

} // namespace

std::optional<QcFrustum::Planes> viewPlanes(const QcCamera &camera, double width, double height)
{
    if (width <= 0.0 || height <= 0.0) {
        return std::nullopt;
    }
    return QcFrustum::planes(camera.projectionMatrix(float(width / height), farPlane) * camera.viewMatrix());
}

QcAabb viewBox(const QcClientEntity &entity, const QcContentRegistry &content)
{
    if (entity.isHanging()) {
        return grown(entity.hanging.box(), margin);
    }

    const QcMobDefinition *creature = entity.creature(content);
    const double half = (creature ? creature->width : 1.0) * 0.5;
    const double height = creature ? creature->height : 2.0;
    const QcVec3 &from = entity.previousPosition;
    const QcVec3 &to = entity.position;
    const QcAabb body{{std::min(from.x, to.x) - half, std::min(from.y, to.y), std::min(from.z, to.z) - half},
                      {std::max(from.x, to.x) + half, std::max(from.y, to.y) + height, std::max(from.z, to.z) + half}};
    return grown(body, margin);
}

bool mayBeSeen(const std::optional<QcFrustum::Planes> &planes, const QcAabb &box)
{
    return !planes || QcFrustum::intersectsBox(*planes, toVector(box.min), toVector(box.max));
}

} // namespace QcEntityCulling
