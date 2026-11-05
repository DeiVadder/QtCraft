// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/figures/QcEntityPose.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/core/QcVec3.h"

#include <QHash>

/*! \brief Boats and their drivers as the client draws them in a frame (decision E-920): a boat
    where the mirror shows it (interpolated, bobbing while it floats) and its paddles rowing
    while its driver paddles (the boat's QcMsg::Rowing bit), and every other player who sits on a
    boat's driver seat drawn there facing the boat's way, their hands on the paddles in the same
    stroke. */
namespace QcBoatRiders {

/*! \brief A boat as drawn this frame: its feet (with the bob), its yaw, the moment of the
    paddles' stroke (QcBoatModel::rowPhase, negative at rest). \qcfields */
struct Drawn
{
    QcVec3 position;
    double yaw = 0.0;
    double rowPhase = -1.0;
};

/*! \brief Where a driver sits as drawn this frame: the boat, the feet on its seat (bobbing with
    it), the boat's yaw and the paddles' stroke. \qcfields */
struct Seat
{
    quint32 boat = 0;
    QcVec3 feet;
    double yaw = 0.0;
    double rowPhase = -1.0;
};

/*! A driver whose seat lies further than this from their drawn place sits in no boat (they got out
    or sits in a cart). */
inline constexpr double seatReach = 0.5;

/*! Returns the boat \a entity at \a alpha between its last two ticks and at \a seconds of the
    client clock. */
[[nodiscard]] Drawn drawn(const QcClientEntity &entity, double alpha, double seconds);
/*! Returns the seat of a driver in the boat \a boatId drawn as \a boat. */
[[nodiscard]] Seat seatIn(quint32 boatId, const Drawn &boat);
/*! Returns the seats of the riding players among \a entities that sit on a boat's driver seat,
    by player id, at \a alpha between the last two ticks and at \a seconds of the client clock. */
[[nodiscard]] QHash<quint32, Seat> seats(const QHash<quint32, QcClientEntity> &entities, double alpha, double seconds);
/*! \brief A driver figure as drawn on its seat: its pose, its feet and the way it faces. \qcfields
 */
struct Seated
{
    QcEntityPose pose;
    QcVec3 feet;
    double yaw = 0.0;
};
/*! Returns the driver figure on \a seat: its \a pose sitting in the boat and rowing with it, the
    head along \a viewYaw, the feet on the seat and facing the boat's way. */
[[nodiscard]] Seated seated(QcEntityPose pose, const Seat &seat, double viewYaw);
/*! Returns how far the head of a driver looking along \a viewYaw turns against their body, which
    faces the boat's \a boatYaw (degrees, wrapped to -180..180). */
[[nodiscard]] double headYaw(double viewYaw, double boatYaw);

} // namespace QcBoatRiders
