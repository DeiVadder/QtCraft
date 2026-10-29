// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QMatrix4x4>
#include <QVector3D>

// First-person camera. Yaw 0 looks along -Z, positive yaw turns right; angles in degrees.
//
// The position is in double: far from the origin a float holds a block coordinate only to metres
// (decision E-883). Everything drawn is placed relative to the camera (relative()), and the view
// matrix is the camera's rotation alone, so the floats the GPU sees stay small.
struct QcCamera
{
    static constexpr float defaultFieldOfView = 70.0f;
    static constexpr float nearPlane = 0.05f;
    static constexpr float farPlaneMargin = 32.0f;

    QcVec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fieldOfView = defaultFieldOfView;

    QVector3D forward() const;
    // The view of a camera at the origin with this camera's direction: maps positions relative to
    // the camera (relative()) into view space.
    QMatrix4x4 viewRotation() const;
    QMatrix4x4 projectionMatrix(float aspect, float farPlane) const;
    // `world` relative to the camera, subtracted in double before it becomes a float.
    QVector3D relative(const QcVec3 &world) const;
};
