#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

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

// What the player aims at when placing a block: `face` of the block at `clicked`, `hit` (0..1
// inside that block), while looking along `yaw` and `pitch` (degrees, positive is up).
struct Aim
{
    QcBlockPos clicked;
    QcBlockFace face = QcBlockFace::Up;
    QcVec3 hit;
    float yaw = 0.0F;
    float pitch = 0.0F;
};

// Looking further up or down than this, a block whose `facing` allows up and down (pistons) faces
// vertically (decision E-334).
inline constexpr float verticalFacingPitch = 45.0F;

// Horizontal direction ("north", "east", "south", "west") a player with this yaw (degrees) looks.
QString lookDirection(float yaw);
// Direction name of a face ("up" and "down" for the vertical ones).
QString faceName(QcBlockFace face);
QcBlockFace opposite(QcBlockFace face);
// The face a direction name ("north" … "down", "floor", "ceiling") stands for.
[[nodiscard]] std::optional<QcBlockFace> faceFromName(const QString &name);
// The direction to the right of someone looking towards `face` (clockwise seen from above).
[[nodiscard]] QcBlockFace rightOf(QcBlockFace face);

// Where and in which state `block` (any state of it) is placed when the player clicks with `aim`. A slab
// clicked on its open half becomes a double slab in place; a clicked replaceable block (tall grass)
// is replaced as if the new block were placed on the ground below it. std::nullopt when the block cannot go
// there (a torch on a ceiling, a door without room).
[[nodiscard]] std::optional<Placement> placement(const QcBlockAccess &world, QcBlockId block, const Aim &aim);

// `id` with its connection properties recomputed from the neighbours (unchanged for blocks that do
// not connect); rails take their track from the rails around them (QcRails::shapedState), stairs
// their corner from the stairs around them (QcStairShapes::shapedState). Sides towards unloaded
// chunks keep their current value.
QcBlockId connectedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// Whether the block at `pos` (in state `id`) still has its support.
bool isSupported(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// The block `id` at `pos` turns into when it touches its `hardensWith` block on any side (concrete
// powder next to water), std::nullopt otherwise.
[[nodiscard]] std::optional<QcBlockId> hardened(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// The block `id` at `pos` turns into while an opaque full block lies on it (its `coveredInto`: a
// dirt path becoming dirt, decision E-406), std::nullopt otherwise.
[[nodiscard]] std::optional<QcBlockId> covered(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// Whether the other half of the door or bed at `pos` is there (or not loaded).
bool hasPartner(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

// The other half of a door (above or below) or a bed (the head lies in the foot's `facing`
// direction) in state `id` at `pos`.
QcBlockPos otherHalf(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

} // namespace QcBlockRules
