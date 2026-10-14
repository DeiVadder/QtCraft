#pragma once

#include "QcWorldSetup.h"

class QcBlockUpdateQueue;
class QcServerState;

// Applies a world generator's one-time setup to a world that was just created (decision E-221):
// time of day and game rules, persistent creatures, contents of chests and machines, block updates
// (queued like those of a command; they wait for their chunks) and growing saplings. Chunks with
// contents or saplings are generated on the spot. Marks the world as set up.
namespace QcServerSetup {

void apply(QcServerState &state, QcBlockUpdateQueue &updates, const QcWorldSetup &setup);

} // namespace QcServerSetup
