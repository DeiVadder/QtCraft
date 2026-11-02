// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QMatrix4x4>
#include <QVector3D>

/*! \brief First-person camera.

    Yaw 0 looks along -Z, positive yaw turns right; angles in degrees.

    The position is in double: far from the origin a float holds a block coordinate only to
    metres (decision E-883). Everything drawn is placed relative to the camera (relative()), and
    the view matrix is the camera's rotation alone, so the floats the GPU sees stay small. */
struct QcCamera
{
    /*! Vertical field of view of a new camera, in degrees. */
    static constexpr float defaultFieldOfView = 70.0f;
    /*! Distance of the near plane in metres. */
    static constexpr float nearPlane = 0.05f;
    /*! Metres the far plane lies beyond the end of the fog. */
    static constexpr float farPlaneMargin = 32.0f;

    /*! The camera's position. */
    QcVec3 position;
    /*! Yaw in degrees. */
    float yaw = 0.0f;
    /*! Pitch in degrees, positive looks up. */
    float pitch = 0.0f;
    /*! Vertical field of view in degrees. */
    float fieldOfView = defaultFieldOfView;

    /*! Returns the unit vector the camera looks along. */
    QVector3D forward() const;
    /*! Returns the view of a camera at the origin with this camera's direction: maps positions
        relative to the camera (relative()) into view space. */
    QMatrix4x4 viewRotation() const;
    /*! Returns the perspective projection for the width-to-height ratio \a aspect and the far plane
        at \a farPlane metres. */
    QMatrix4x4 projectionMatrix(float aspect, float farPlane) const;
    /*! Returns \a world relative to the camera, subtracted in double before it becomes a float. */
    QVector3D relative(const QcVec3 &world) const;
};
