// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcProjectileDefinition.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "server/entities/QcProjectile.h"

#include <QList>

class QcBlockAccess;

/*! \brief One tick of a projectile's flight (decision E-425), pure so that trajectories and hits
    are testable on their own.

    Gravity and drag change the velocity first; then the projectile moves along the straight
    segment of this tick, swept as a whole against the blocks (its centre, the collision boxes)
    and the bodies (their boxes grown by half its size). The first thing on the segment ends the
    step there, however fast it flies: there are no fixed sub-steps to tunnel through, the
    segment is the sub-step. */
namespace QcProjectileFlight {

/*! It does not hit the entity that shot it before it is this old (it starts inside his box). */
inline constexpr double shooterGraceSeconds = 0.25;
/*! How far a stuck projectile's tip goes into the block (m). */
inline constexpr double stickDepth = 0.1;
/*! A floating projectile (QcProjectileDefinition::floats, decision E-503) in water rises at
    floatRise m/s and rests with its centre floatHeight above the bottom of the uppermost water
    block; a fast one that crosses water on its way lands in it rather than in the floor below,
    and lava burns it up (Outcome::Burnt). \qcshared */
inline constexpr double floatRise = 1.5;
inline constexpr double floatHeight = 0.9;

/*! \brief A body the projectile can hit: a creature or a player. */
struct Body
{
    /*! Its entity id. */
    quint32 id = 0;
    /*! Its box. */
    QcAabb box;
};

/*! \brief How a step of a projectile ended. */
enum class Outcome : quint8 {
    /*! It flies on. */
    Flying,
    /*! It met a body (Step::body) at Step::position. */
    HitBody,
    /*! It met a block (Step::block) and stuck there, or stopped there if it does not stick. */
    HitBlock,
    /*! The segment leads into a chunk that is not loaded: it stays where it is with its velocity
        and flies on once the chunk is there (or ages away). */
    Waiting,
    /*! A floating projectile reached a liquid that is not water (lava) and burns up. */
    Burnt,
};

/*! \brief The result of one step of a projectile. */
struct Step
{
    /*! How the step ended. */
    Outcome outcome = Outcome::Flying;
    /*! Entity id of the body hit (Outcome::HitBody). */
    quint32 body = 0;
    /*! The block hit (Outcome::HitBlock). */
    QcBlockPos block;
    /*! A hit block: the face it was met through and the point on it (world space). \qcshared */
    QcBlockFace face = QcBlockFace::Up;
    QcVec3 point;
};

/*! Returns the direction (unit vector) of a yaw of \a yawDegrees and a pitch of \a pitchDegrees,
    as players look (yaw 0: north, -z; pitch up positive). */
QcVec3 direction(double yawDegrees, double pitchDegrees);
/*! Turns the yaw and pitch of \a projectile along its velocity. */
void turnAlong(QcProjectile &projectile);

/*! Advances \a projectile of the kind \a definition by \a dt seconds through \a world and past
    \a bodies and returns how the step ended; on a hit its position is the point of the hit. Its
    age is left to the caller. */
Step step(const QcBlockAccess &world, QcProjectile &projectile, const QcProjectileDefinition &definition, const QList<Body> &bodies,
          double dt);

/*! Returns the damage of a hit at \a speed m/s: the damage of \a definition per
    QcProjectileDefinition::damageSpeed, times the projectile's \a factor, rounded up. */
double damageAt(const QcProjectileDefinition &definition, double speed, double factor);

} // namespace QcProjectileFlight
