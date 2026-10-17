#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QString>

#include <optional>

// Where the camera sits: in the player's eyes, behind the player, in front looking back, or (a
// test hook for screenshots, not in the key's cycle) to the player's right looking at their side.
enum class QcCameraMode { FirstPerson, Behind, Front, Side };

// Places the camera for a view mode. Third-person cameras pull in when a block lies between the
// player's eyes and the wanted position, so they never look through walls.
namespace QcViewCamera {

inline constexpr double thirdPersonDistance = 4.0;
// Kept between the camera and a blocking surface (the near plane must not cut into it).
inline constexpr double wallMargin = 0.3;

struct Placement
{
    QcVec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

Placement place(const QcVec3 &eye, float yaw, float pitch, QcCameraMode mode, const QcBlockAccess &world);

QcCameraMode next(QcCameraMode mode);
const char *name(QcCameraMode mode);
// "first", "behind", "front" or "side"; std::nullopt otherwise.
[[nodiscard]] std::optional<QcCameraMode> fromName(const QString &text);

} // namespace QcViewCamera
