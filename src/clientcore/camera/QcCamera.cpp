// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcCamera.h"

#include <QtMath>

QVector3D QcCamera::forward() const
{
    const float yawRad = qDegreesToRadians(yaw);
    const float pitchRad = qDegreesToRadians(pitch);
    return {std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad), -std::cos(yawRad) * std::cos(pitchRad)};
}

QMatrix4x4 QcCamera::viewRotation() const
{
    QMatrix4x4 view;
    // A clockwise roll of the camera turns the world anticlockwise in view space.
    if (roll != 0.0f) {
        view.rotate(roll, 0.0f, 0.0f, 1.0f);
    }
    view.lookAt(QVector3D(), forward(), QVector3D(0.0f, 1.0f, 0.0f));
    return view;
}

QMatrix4x4 QcCamera::projectionMatrix(float aspect, float farPlane) const
{
    QMatrix4x4 projection;
    projection.perspective(fieldOfView, aspect, nearPlane, farPlane);
    return projection;
}

QVector3D QcCamera::relative(const QcVec3 &world) const
{
    const QcVec3 offset = world - position;
    return {static_cast<float>(offset.x), static_cast<float>(offset.y), static_cast<float>(offset.z)};
}
