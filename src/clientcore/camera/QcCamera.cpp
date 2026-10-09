#include "QcCamera.h"

#include <QtMath>

QVector3D QcCamera::forward() const
{
    const float yawRad = qDegreesToRadians(yaw);
    const float pitchRad = qDegreesToRadians(pitch);
    return {std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad), -std::cos(yawRad) * std::cos(pitchRad)};
}

QMatrix4x4 QcCamera::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(position, position + forward(), QVector3D(0.0f, 1.0f, 0.0f));
    return view;
}

QMatrix4x4 QcCamera::projectionMatrix(float aspect, float farPlane) const
{
    QMatrix4x4 projection;
    projection.perspective(fieldOfView, aspect, nearPlane, farPlane);
    return projection;
}
