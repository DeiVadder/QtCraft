#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QList>

class QcBlastField;

// How an explosion reaches blocks and bodies (decision E-602), pure so that craters and exposure are
// testable on their own. Like the reference game: rays leave the centre towards the 1352 points on
// the surface of a 16 × 16 × 16 grid, each with a force of power × (0.7 to 1.3), and walk in steps
// of 0.3 blocks; every step costs 0.225 and a cell that is not air (resistance + 0.3) × 0.3 of the
// force, and a cell the ray still has force for after that breaks. The dice of a ray come from the
// world's seed, the cell of the centre and the ray, so the same explosion in the same world always
// leaves the same crater. A liquid soaks up a whole ray (resistance 100): a blast under water
// breaks nothing. A cell of a chunk that is not loaded stops its ray.
namespace QcExplosionRays {

// Steps of the grid along each edge of the cube the rays point at.
inline constexpr int grid = 16;
inline constexpr double stepLength = 0.3;
inline constexpr double stepCost = 0.225;
// Bodies feel a blast up to twice its power away.
inline constexpr double reachFactor = 2.0;

struct Blast
{
    QcVec3 centre;
    double power = 4.0;
    quint64 seed = 0;
};

// The farthest a ray of `power` can break a block, in whole blocks (the size of a QcBlastField).
int reach(double power);
// The cells the blast breaks, nearest to the centre first (ties in the order of x, y, z), at most
// `limit`; unbreakable cells (QcBlastDefinition::unbreakable) never.
QList<QcBlockPos> broken(QcBlastField &field, const Blast &blast, qsizetype limit);
// The share (0..1) of points spread over `box` that see the centre past solid blocks.
double exposure(QcBlastField &field, const QcVec3 &centre, const QcAabb &box);

} // namespace QcExplosionRays
