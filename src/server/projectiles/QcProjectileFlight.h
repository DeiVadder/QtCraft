#pragma once

#include "common/content/definitions/QcProjectileDefinition.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "server/entities/QcProjectile.h"

#include <QList>

class QcBlockAccess;

// One tick of a projectile's flight (decision E-425), pure so that trajectories and hits are
// testable on their own. Gravity and drag change the velocity first; then the projectile moves
// along the straight segment of this tick, swept as a whole against the blocks (its centre, the
// collision boxes) and the bodies (their boxes grown by half its size). The first thing on the
// segment ends the step there, however fast it flies: there are no fixed sub-steps to tunnel
// through, the segment is the sub-step.
namespace QcProjectileFlight {

// It does not hit the entity that shot it before it is this old (it starts inside his box).
inline constexpr double shooterGraceSeconds = 0.25;
// How far a stuck projectile's tip goes into the block (m).
inline constexpr double stickDepth = 0.1;

// A body the projectile can hit: a creature or a player.
struct Body
{
    quint32 id = 0;
    QcAabb box;
};

enum class Outcome : quint8 {
    Flying,
    // It met a body (Step::body) at Step::position.
    HitBody,
    // It met a block (Step::block) and stuck there, or stopped there if it does not stick.
    HitBlock,
    // The segment leads into a chunk that is not loaded: it stays where it is with its velocity
    // and flies on once the chunk is there (or ages away).
    Waiting,
};

struct Step
{
    Outcome outcome = Outcome::Flying;
    quint32 body = 0;
    QcBlockPos block;
};

// The direction (unit vector) of a yaw and pitch in degrees, as players look (yaw 0: north, -z;
// pitch up positive), and back.
QcVec3 direction(double yawDegrees, double pitchDegrees);
void turnAlong(QcProjectile &projectile);

// Advances `projectile` by `dt` seconds; on a hit its position is the point of the hit. Its age is
// left to the caller.
Step step(const QcBlockAccess &world, QcProjectile &projectile, const QcProjectileDefinition &definition, const QList<Body> &bodies,
          double dt);

// The damage of a hit at `speed` m/s: the definition's damage per QcProjectileDefinition::damageSpeed,
// times the projectile's factor, rounded up.
double damageAt(const QcProjectileDefinition &definition, double speed, double factor);

} // namespace QcProjectileFlight
