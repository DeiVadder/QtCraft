// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/physics/QcPose.h"

struct QcMobDefinition;

/*! \brief Soft collision between players and creatures (decision E-882), shared by the server
    (authoritative) and the client's prediction of its player, so both push the player alike.

    Bodies are no walls: two that overlap push each other apart, horizontally and the more the
    deeper they overlap, so a player still squeezes past a villager and nudges an animal in its
    pen. A body is a standing cylinder around its feet: the push of one body on another runs
    along the line between their feet on the ground, with the share of the overlap of their half
    widths (1 on the same spot, 0 where they just touch), and only while their heights overlap. */
namespace QcBodyPush {

/*! \brief A body as the push sees it. */
struct Body
{
    /*! The middle of its bottom. */
    QcVec3 feet;
    /*! Half its width: the cylinder's radius. */
    double halfWidth = 0.0;
    /*! Its height. */
    double height = 0.0;
};

/*! The horizontal speed in m/s at which the creatures push a player out of a full overlap (less
    in proportion to a smaller one). Below the walking speed (QcPhysics::walkSpeed 4.3), so
    walking on gets through; a sneaking player is held back by a creature right in front of him. */
inline constexpr double playerSpeed = 3.0;

/*! On the same spot a push has no direction: the player goes towards +x, the creature towards
    -x. \qcshared */
inline constexpr QcVec3 playerParting{1.0, 0.0, 0.0};
inline constexpr QcVec3 creatureParting{-1.0, 0.0, 0.0};

/*! Returns the push of \a other on \a self: away from \a other along the ground, as long as the
    share of their overlap (see above); zero when they do not overlap. On the same spot (to a
    tenth of a millimetre) along \a parting, the same on server and client. */
[[nodiscard]] QcVec3 between(const Body &self, const Body &other, const QcVec3 &parting = playerParting);
/*! Returns the velocity of the push on a player from \a sum, the sum of the pushes of the bodies he
    overlaps (between): playerSpeed times the sum, no faster than playerSpeed, so that a crowd
    does not throw him faster than he walks. */
[[nodiscard]] QcVec3 playerVelocity(const QcVec3 &sum);

/*! Returns \a push on a walker turned so that it goes around the body it meets instead of
    stalling in front of it: where the push works against the walker's \a wish, its sideways
    share (the push turned by a quarter towards the wish, to the left when the wish runs straight
    into it) is added. A creature whose path leads through a player standing still walks around
    him. */
[[nodiscard]] QcVec3 passing(const QcVec3 &push, const QcVec3 &wish);

/*! Returns whether a creature of \a definition in \a pose pushes players: walkers, swimmers and
    hoverers do, flyers (fireflies, decision E-057) and creatures lying in bed (decision E-411)
    do not. The client knows both from the content and the entity flags. */
[[nodiscard]] bool pushesPlayers(const QcMobDefinition &definition, QcPose pose);

/*! Returns the body of a creature of \a definition with its feet at \a feet (the full size also
    for young ones, like the crowd of the creatures, decision E-401). */
[[nodiscard]] Body creature(const QcMobDefinition &definition, const QcVec3 &feet);
/*! Returns the body of a player with his feet at \a feet. */
[[nodiscard]] Body player(const QcVec3 &feet);

} // namespace QcBodyPush
