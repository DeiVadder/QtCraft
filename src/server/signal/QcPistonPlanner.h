#pragma once

#include "QcSignalPower.h"

#include <QList>

#include <optional>

// What a piston would do now (decision E-337), worked out against the world as it is, before
// anything moves: pistons that act in the same step are planned on the same world and then checked
// against each other (QcPistonPlanner::resolveConflicts).
struct QcPistonPlan
{
    QcBlockPos base;
    QcBlockFace facing = QcBlockFace::North;
    bool extend = true;
    bool sticky = false;
    // Blocks that move one cell along `facing` (extending) or back against it (a sticky piston
    // pulling), nearest to the piston first.
    QList<QcBlockPos> moved;
    // Blocks in the way that break with their drops (plants, torches, dust, liquids).
    QList<QcBlockPos> broken;

    QcBlockPos head() const { return qcNeighbour(base, facing); }
    // Every cell the plan reads or writes: the piston, its head, where blocks come from and go to.
    QList<QcBlockPos> touched() const;
};

namespace QcPistonPlanner {

// The push of the piston at `base`: the line of blocks in front of it up to air or a block that
// breaks. std::nullopt when it is longer than the push limit, holds an immovable block (bedrock,
// an extended piston, a piston head) or reaches into an unloaded chunk or out of the world.
std::optional<QcPistonPlan> extension(QcSignalPower power, QcBlockPos base, QcSignalState piston,
                                      const QcSignalDefinition &definition);
// Retracting: a sticky piston pulls the block two cells in front of it back into its head's cell
// unless `letGo` (its extension had not finished: block spitting) or that block cannot move.
QcPistonPlan retraction(QcSignalPower power, QcBlockPos base, QcSignalState piston, const QcSignalDefinition &definition,
                        bool letGo);

// Drops every plan that touches a cell another plan of the same step touches: two pistons that
// want the same block or cell both stay where they are. The rule looks only at cells, never at
// directions or coordinates, so it gives the same answer in every rotation and place.
QList<QcPistonPlan> resolveConflicts(const QList<QcPistonPlan> &plans);

} // namespace QcPistonPlanner
