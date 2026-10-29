// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/figures/QcCapeSwing.h"
#include "clientcore/figures/QcEntityPose.h"
#include "clientcore/preview/QcPreviewCamera.h"

#include <QString>

#include <optional>


// Poses of the character preview: the animations to check a look with (standing with the idle
// sway, walking on the spot, mining) and the head following the mouse pointer. Pure functions of
// their inputs, so the preview is deterministic and testable.
namespace QcPreviewPose {

enum class Animation { Stand, Walk, Mine };

// "stand", "walk", "mine".
QString name(Animation animation);
[[nodiscard]] std::optional<Animation> fromName(const QString &name);

// Length of one mining swing (as in the world, E-033) and the time from one swing to the next.
inline constexpr double swingSeconds = 0.3;
inline constexpr double swingPeriod = 0.5;
// How far the head turns to follow the pointer, against the body.
inline constexpr double maxGazeYaw = 60.0;
inline constexpr double maxGazePitch = 35.0;
// The head turns towards a new target with this rate (per second, share of the way left).
inline constexpr double gazeRate = 8.0;

// Head angles against the body in degrees: yaw positive to the figure's right, pitch positive up.
struct Gaze
{
    double yaw = 0.0;
    double pitch = 0.0;

    bool operator==(const Gaze &) const = default;
};

// The figure's pose `seconds` into an animation, with the head turned by `gaze`.
QcEntityPose pose(Animation animation, double seconds, Gaze gaze);

// How the figure moves for its cape: walking forwards on the spot, else standing.
QcCapeDrive capeDrive(Animation animation);

// Where the head looks to follow a pointer at `pointer` in the view (x and y in -1..1 from the
// centre, y up; `aspect` is width / height). The pointer is taken a third of the way from the
// figure to the camera, so the head turns clearly towards it. Seen from behind, the head turns back
// to the front instead of twisting round.
Gaze gazeAt(QcPreviewCamera camera, double pointerX, double pointerY, double aspect);

// The head moved from `current` towards `target` over `seconds` (smooth, never past the target).
Gaze approach(Gaze current, Gaze target, double seconds);

} // namespace QcPreviewPose
