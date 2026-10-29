// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcEntityLight.h"
#include "clientcore/meshing/QcEntityMesh.h"
#include "common/core/QcVec3.h"

#include <QRectF>

// The model of a projectile (an arrow, decision E-425): the item's icon, which lies on its diagonal
// from the tail at the bottom left to the head at the top right, drawn as two crossed squares
// whose diagonals run along the flight, so the head points where it flies and the fletching
// shows from every side, like the reference's arrows.
namespace QcProjectileMesher {

// Length of the drawn projectile from tail to head (m).
inline constexpr double length = 0.5;

// Where and how a projectile is drawn this frame: its centre, yaw and pitch (degrees, as players
// look) and its light.
struct Placement
{
    QcVec3 centre;
    double yaw = 0.0;
    double pitch = 0.0;
    QcEntityLight light;
};

void append(QcEntityMesh &mesh, const Placement &placement, const QRectF &icon);

} // namespace QcProjectileMesher
