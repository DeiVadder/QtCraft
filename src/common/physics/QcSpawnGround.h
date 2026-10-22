#pragma once

#include "common/content/registry/QcContentRegistry.h"

// Where creatures that walk may appear by themselves (decision E-836), the same for every kind
// instead of a block list per creature: the ground is a solid block whose top face is full and that
// is drawn opaque (no glass, leaves or ice, no lower slab, path or farmland; an upper slab and a
// double slab carry), and neither ground nor the cells of feet and head hurt or hold a body
// (QcBlockDefinition::keepsSpawnsAway). The cells of feet and head are free: not solid, no liquid,
// no rail. A spawn rule's `blocks` may still narrow the ground down (peaceful animals on grass).
namespace QcSpawnGround {

// Whether a creature may appear standing on the block state `ground`.
bool carries(const QcContentRegistry &content, QcBlockId ground);
// Whether a creature's feet or head may be in the block state `cell` when it appears.
bool leavesRoom(const QcContentRegistry &content, QcBlockId cell);

// How many states of a block carry spawns (the handbook's tables): all, some (slabs: only the upper
// and the double one) or none.
enum class Carrying : quint8 { All, Some, None };
Carrying carryingOf(const QcContentRegistry &content, quint32 type);

} // namespace QcSpawnGround
