// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtMath>

#include <cmath>

// Rotations the entity meshers place model, item and hand points with.
namespace QcEntityMesher {

// Rotation around the X axis (positive moves -Y towards -Z), then around Z (positive moves -Y
// towards +X) and then around Y (positive moves -Z towards +X), all in degrees. Sine and cosine are
// taken once, not per vertex.
struct Rotate
{
    double cosPitch;
    double sinPitch;
    double cosRoll;
    double sinRoll;
    double cosYaw;
    double sinYaw;

    Rotate(double pitch, double roll, double yaw = 0.0)
        : cosPitch(std::cos(qDegreesToRadians(pitch)))
        , sinPitch(std::sin(qDegreesToRadians(pitch)))
        , cosRoll(std::cos(qDegreesToRadians(roll)))
        , sinRoll(std::sin(qDegreesToRadians(roll)))
        , cosYaw(std::cos(qDegreesToRadians(yaw)))
        , sinYaw(std::sin(qDegreesToRadians(yaw)))
    {
    }

    QcVec3 operator()(const QcVec3 &p) const
    {
        const QcVec3 q{p.x, p.y * cosPitch - p.z * sinPitch, p.y * sinPitch + p.z * cosPitch};
        const QcVec3 r{q.x * cosRoll - q.y * sinRoll, q.x * sinRoll + q.y * cosRoll, q.z};
        return {r.x * cosYaw - r.z * sinYaw, r.y, r.x * sinYaw + r.z * cosYaw};
    }
};

// Rotation around the Y axis by a yaw in degrees: the model faces -Z; yaw 90 faces +X.
struct Turn
{
    double cosYaw;
    double sinYaw;

    explicit Turn(double yawDegrees)
        : cosYaw(std::cos(qDegreesToRadians(yawDegrees)))
        , sinYaw(std::sin(qDegreesToRadians(yawDegrees)))
    {
    }

    QcVec3 operator()(const QcVec3 &p) const { return {p.x * cosYaw - p.z * sinYaw, p.y, p.x * sinYaw + p.z * cosYaw}; }
};

} // namespace QcEntityMesher
