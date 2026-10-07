#pragma once

#include <QMatrix4x4>
#include <QVector3D>

// First-person camera. Yaw 0 looks along -Z, positive yaw turns right; angles in degrees.
struct QcCamera
{
    static constexpr float defaultFieldOfView = 70.0f;
    static constexpr float nearPlane = 0.05f;
    static constexpr float farPlaneMargin = 32.0f;

    QVector3D position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fieldOfView = defaultFieldOfView;

    QVector3D forward() const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float aspect, float farPlane) const;
};
