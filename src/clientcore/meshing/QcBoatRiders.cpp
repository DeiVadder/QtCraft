#include "clientcore/meshing/QcBoatRiders.h"

#include "clientcore/meshing/QcBoatModel.h"
#include "common/protocol/QcEntityMessages.h"
#include "common/world/QcBoatBody.h"

#include <cmath>

namespace QcBoatRiders {

namespace {

QcVec3 lerp(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

double lerpYaw(float from, float to, double t)
{
    const float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return static_cast<double>(from + delta * static_cast<float>(t));
}

} // namespace

Drawn drawn(const QcClientEntity &entity, double alpha, double seconds)
{
    Drawn boat{lerp(entity.previousPosition, entity.position, alpha), lerpYaw(entity.previousYaw, entity.yaw, alpha), -1.0};
    if ((entity.flags & QcMsg::InWater) != 0) {
        boat.position.y += QcBoatModel::bob(seconds, entity.id);
    }
    boat.rowPhase = QcBoatModel::rowPhase(seconds, (entity.flags & QcMsg::Rowing) != 0);
    return boat;
}

Seat seatIn(quint32 boatId, const Drawn &boat)
{
    return {boatId, QcBoatBody::driverSeat(boat.position, boat.yaw), boat.yaw, boat.rowPhase};
}

QHash<quint32, Seat> seats(const QHash<quint32, QcClientEntity> &entities, double alpha, double seconds)
{
    QHash<quint32, Seat> found;
    QList<Seat> boats;
    for (const QcClientEntity &entity : entities) {
        if (entity.isBoat()) {
            boats.append(seatIn(entity.id, drawn(entity, alpha, seconds)));
        }
    }
    if (boats.isEmpty()) {
        return found;
    }

    for (const QcClientEntity &entity : entities) {
        if (!entity.isPlayer() || (entity.flags & QcMsg::Riding) == 0) {
            continue;
        }
        // The bob moves the drawn seat a little; the driver's own place does not bob.
        const QcVec3 at = lerp(entity.previousPosition, entity.position, alpha);
        for (const Seat &seat : std::as_const(boats)) {
            if ((seat.feet - at).length() <= seatReach) {
                found.insert(entity.id, seat);
                break;
            }
        }
    }
    return found;
}

Seated seated(QcEntityPose pose, const Seat &seat, double viewYaw)
{
    pose.inBoat = true;
    pose.rowPhase = seat.rowPhase;
    pose.headYaw = static_cast<float>(headYaw(viewYaw, seat.yaw));
    return {pose, seat.feet, seat.yaw};
}

double headYaw(double viewYaw, double boatYaw)
{
    const double turned = std::fmod(viewYaw - boatYaw + 540.0, 360.0) - 180.0;
    return turned < -180.0 ? turned + 360.0 : turned;
}

} // namespace QcBoatRiders
