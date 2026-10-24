#include "common/physics/QcBodyPush.h"

#include "common/content/definitions/QcMobDefinition.h"
#include "common/physics/QcPhysics.h"

#include <cmath>

namespace QcBodyPush {

QcVec3 between(const Body &self, const Body &other, const QcVec3 &parting)
{
    if (self.feet.y >= other.feet.y + other.height || other.feet.y >= self.feet.y + self.height) {
        return {};
    }

    const double reach = self.halfWidth + other.halfWidth;
    const double dx = self.feet.x - other.feet.x;
    const double dz = self.feet.z - other.feet.z;
    const double distance = std::hypot(dx, dz);
    if (reach <= 0.0 || distance >= reach) {
        return {};
    }

    // On the same spot the direction would be noise.
    const QcVec3 away = distance > 1e-4 ? QcVec3{dx / distance, 0.0, dz / distance} : parting;
    return away * (1.0 - distance / reach);
}

QcVec3 playerVelocity(const QcVec3 &sum)
{
    const double length = sum.length();
    return length > 1.0 ? sum * (playerSpeed / length) : sum * playerSpeed;
}

QcVec3 passing(const QcVec3 &push, const QcVec3 &wish)
{
    if (push.dot(wish) >= 0.0) {
        return push;
    }
    const QcVec3 side{-push.z, 0.0, push.x};
    return push + (side.dot(wish) < 0.0 ? side * -1.0 : side);
}

bool pushesPlayers(const QcMobDefinition &definition, QcPose pose)
{
    return !definition.flying && pose != QcPose::Lying;
}

Body creature(const QcMobDefinition &definition, const QcVec3 &feet)
{
    return {feet, definition.width * 0.5, definition.height};
}

Body player(const QcVec3 &feet)
{
    return {feet, QcPhysics::playerWidth * 0.5, QcPhysics::playerHeight};
}

} // namespace QcBodyPush
