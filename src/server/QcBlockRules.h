#pragma once

#include "QcBlockAccess.h"
#include "QcBlockFace.h"
#include "QcVec3.h"

#include <optional>

// Server rules that derive block states from the surroundings: the state a placed block takes from
// the player's view and the clicked face, connections of fences, walls and panes, and whether a
// block still has the support it needs. Behaviour follows from data: the block's properties
// (facing, half, type, attach, hinge, open, north/east/south/west, up) and its definition flags.
namespace QcBlockRules {

struct Placement
{
    QcBlockPos pos;
    QcBlockId state = qcAirBlockId;
};

// Horizontal direction ("north", "east", "south", "west") a player with this yaw (degrees) looks.
QString lookDirection(float yaw);
// Direction name of a face ("up" and "down" for the vertical ones).
QString faceName(QcBlockFace face);
QcBlockFace opposite(QcBlockFace face);

// Where and in which state `block` (any state of it) is placed when the player clicks `face` of the
// block at `clicked`, aiming at `hit` (0..1 inside that block) while looking along `yaw`. A slab
// clicked on its open half becomes a double slab in place; a clicked replaceable block (tall grass)
// is replaced as if the new block were placed on the ground below it. std::nullopt when the block cannot go
// there (a torch on a ceiling, a door without room).
std::optional<Placement> placement(const QcBlockAccess &world, QcBlockId block, QcBlockPos clicked, QcBlockFace face,
                                   const QcVec3 &hit, float yaw);

// `id` with its connection properties recomputed from the neighbours (unchanged for blocks that do
// not connect). Sides towards unloaded chunks keep their current value.
QcBlockId connectedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// Whether the block at `pos` (in state `id`) still has its support.
bool isSupported(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// Whether the other half of the door at `pos` is there (or not loaded).
bool hasDoorPartner(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// The other half of a door in state `id` at `pos`.
QcBlockPos otherDoorHalf(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

} // namespace QcBlockRules
