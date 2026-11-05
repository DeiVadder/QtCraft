// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QString>

#include <optional>

/*! \brief Where the camera sits: in the player's eyes, behind the player, in front looking back,
    or (a test hook for screenshots, not in the key's cycle) to the player's right looking at
    their side. */
enum class QcCameraMode {
    FirstPerson, // in the player's eyes
    Behind,      // behind the player
    Front,       // in front, looking back
    Side         // to the player's right (test hook)
};

/*! \brief Places the camera for a view mode.

    Third-person cameras pull in when a block lies between the player's eyes and the wanted
    position, so they never look through walls. */
namespace QcViewCamera {

/*! Metres between the eye and a third-person camera, unless a block is in the way. */
inline constexpr double thirdPersonDistance = 4.0;
/*! Kept between the camera and a blocking surface (the near plane must not cut into it). */
inline constexpr double wallMargin = 0.3;

/*! \brief Where the camera is and where it looks. */
struct Placement
{
    /*! The camera's position. */
    QcVec3 position;
    /*! Its yaw in degrees. */
    float yaw = 0.0f;
    /*! Its pitch in degrees. */
    float pitch = 0.0f;
};

/*! Returns where the camera of \a mode goes for a player whose eye is at \a eye looking along \a
    yaw and \a pitch, pulled in where a block of \a world is in the way. */
Placement place(const QcVec3 &eye, float yaw, float pitch, QcCameraMode mode, const QcBlockAccess &world);

/*! Returns the view mode after \a mode in the key's cycle: first person, behind, front. */
QcCameraMode next(QcCameraMode mode);
/*! Returns the name of \a mode: "first", "behind", "front" or "side". */
const char *name(QcCameraMode mode);
/*! Returns the mode named \a text: "first", "behind", "front" or "side"; std::nullopt otherwise. */
[[nodiscard]] std::optional<QcCameraMode> fromName(const QString &text);

} // namespace QcViewCamera
