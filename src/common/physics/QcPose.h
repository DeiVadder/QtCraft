// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcVec3.h"

#include <QtGlobal>

// How a body holds itself (decision E-423): its box, the height of its eyes and its animation
// follow from it. A villager in its bed lies (decision E-411); tamed animals will sit (stage 52).
enum class QcPose : quint8 {
    Standing,
    // On its back, its feet towards where it faces (QcPhysics::lyingBoxAt).
    Lying,
    // On its haunches: the legs point forward, the box stays the standing one (as in the reference).
    Sitting,
};

namespace QcPoses {

// The share of its height at which a standing or sitting body has its eyes.
inline constexpr double eyeShare = 0.85;

// The box of a body of `width` and `height` with its feet at `feet`, facing `yaw`, in `pose`.
QcAabb boxAt(QcPose pose, const QcVec3 &feet, float yaw, double width, double height);
// Where its eyes are: the middle of its box while it lies, `eyeShare` of its height above its feet
// otherwise.
QcVec3 eyesAt(QcPose pose, const QcVec3 &feet, float yaw, double width, double height);

} // namespace QcPoses
