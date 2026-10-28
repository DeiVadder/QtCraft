#pragma once

#include "clientcore/meshing/QcEntityPose.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/core/QcVec3.h"

#include <QHash>

// Boats and their drivers as the client draws them in a frame (decision E-920): a boat where the
// mirror shows it (interpolated, bobbing while it floats) and its paddles rowing while its driver
// paddles (the boat's QcMsg::Rowing bit), and every other player who sits on a boat's driver seat
// drawn there facing the boat's way, his hands on the paddles in the same stroke.
namespace QcBoatRiders {

// A boat as drawn this frame: its feet (with the bob), its yaw, the moment of the paddles' stroke
// (QcBoatModel::rowPhase, negative at rest).
struct Drawn
{
    QcVec3 position;
    double yaw = 0.0;
    double rowPhase = -1.0;
};

// Where a driver sits as drawn this frame: the boat, the feet on its seat (bobbing with it), the
// boat's yaw and the paddles' stroke.
struct Seat
{
    quint32 boat = 0;
    QcVec3 feet;
    double yaw = 0.0;
    double rowPhase = -1.0;
};

// A driver whose seat lies further than this from his drawn place sits in no boat (he got out or
// sits in a cart).
inline constexpr double seatReach = 0.5;

// The boat `entity` at `alpha` between its last two ticks and at `seconds` of the client clock.
[[nodiscard]] Drawn drawn(const QcClientEntity &entity, double alpha, double seconds);
// The seat of a driver in a boat drawn as `boat`.
[[nodiscard]] Seat seatIn(quint32 boatId, const Drawn &boat);
// The seats of the riding players among `entities` that sit on a boat's driver seat, by player id.
[[nodiscard]] QHash<quint32, Seat> seats(const QHash<quint32, QcClientEntity> &entities, double alpha, double seconds);
// A driver figure on `seat`: its pose (`pose` sitting in the boat, rowing with it, the head along
// `viewYaw`), its feet and the way it faces (the boat's).
struct Seated
{
    QcEntityPose pose;
    QcVec3 feet;
    double yaw = 0.0;
};
[[nodiscard]] Seated seated(QcEntityPose pose, const Seat &seat, double viewYaw);
// How far the head of a driver looking along `viewYaw` turns against his body, which faces the
// boat's `boatYaw` (degrees, wrapped to -180..180).
[[nodiscard]] double headYaw(double viewYaw, double boatYaw);

} // namespace QcBoatRiders
