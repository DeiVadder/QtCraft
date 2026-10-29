// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcEntityCulling.h"

#include "clientcore/session/QcClientEntity.h"

#include <algorithm>

namespace QcEntityCulling {

namespace {

// `v` relative to `eye`, subtracted in double.
QVector3D relative(const QcVec3 &v, const QcVec3 &eye)
{
    const QcVec3 offset = v - eye;
    return {static_cast<float>(offset.x), static_cast<float>(offset.y), static_cast<float>(offset.z)};
}

QcAabb grown(const QcAabb &box, double by)
{
    const QcVec3 delta{by, by, by};
    return {box.min - delta, box.max + delta};
}

} // namespace

std::optional<ViewPlanes> viewPlanes(const QcCamera &camera, double width, double height)
{
    if (width <= 0.0 || height <= 0.0) {
        return std::nullopt;
    }
    return ViewPlanes{QcFrustum::planes(camera.projectionMatrix(static_cast<float>(width / height), farPlane) * camera.viewRotation()),
                      camera.position};
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

bool mayBeSeen(const std::optional<ViewPlanes> &planes, const QcAabb &box)
{
    return !planes || QcFrustum::intersectsBox(planes->planes, relative(box.min, planes->eye), relative(box.max, planes->eye));
}

} // namespace QcEntityCulling
