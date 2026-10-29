// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <array>

// View-frustum culling of axis-aligned boxes (chunk sections) against the clip planes of a
// view-projection matrix.
namespace QcFrustum {

using Planes = std::array<QVector4D, 6>;

// Clip planes as (a, b, c, d) with a x + b y + c z + d >= 0 inside: left, right, bottom, top, far
// and w > 0 (in front of the camera). Works for depth ranges [-1, 1] and [0, 1].
Planes planes(const QMatrix4x4 &viewProjection);

// False only when the box lies entirely outside one plane, i.e. when its corner furthest along
// that plane's normal does. Boxes crossing a corner of the frustum may count as visible.
bool intersectsBox(const Planes &planes, QVector3D boxMin, QVector3D boxMax);

} // namespace QcFrustum
