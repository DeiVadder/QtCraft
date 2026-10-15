#pragma once

#include <QStringList>

class QcServerWorld;

// The rules between the blocks of a world and its block entities, checked in one place (decision
// E-357; tests call it after their scenarios, a broken rule is a bug):
// - a container exists exactly when its block has `container`, with the block's number of slots
//   (missing only while the table of containers is full); likewise a machine for `machine`;
// - a sapling timer only on a sapling block; a hopper cooldown only in a hopper's container;
// - what a component remembers fits its kind (burnout: torch, powered: opener, extension: piston);
// - the counts, the order of saplings and the chunks of scheduled ticks agree with the entries;
// - every burning fire of the fire queue lies on a fire block of a loaded chunk, within the caps
//   per chunk and world (decision E-392).
// Only loaded chunks are compared with their blocks.
namespace QcBlockEntityChecks {

// Every broken rule as a line naming the position; empty when all hold.
QStringList problems(const QcServerWorld &world);

} // namespace QcBlockEntityChecks
