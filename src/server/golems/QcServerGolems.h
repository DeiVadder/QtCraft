#pragma once

#include "common/core/QcBlockPos.h"

#include <QtGlobal>

#include <array>
#include <optional>

class QcBlockAccess;
class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcItemDefinition;
struct QcMobDefinition;
struct QcServerClient;

// Players and golems (decision E-471): building a golem from blocks and repairing one. A golem
// kind with `build` (QcGolemDefinition) comes alive when a player places its head block on the
// middle of four body blocks standing upright in a T, the T along either horizontal axis, with
// air beside the head and beside the foot (an iron golem: four iron blocks and a carved pumpkin
// in any of the four directions); the blocks vanish and a golem that never attacks players
// stands in their place. A golem kind with a `repairItem` is healed by `repairHealth` when a
// player uses that item on it (one of the stack, except in creative mode).
class QcServerGolems
{
public:
    // The cells of a built golem: its head, the middle and foot of its body and its two arms.
    struct Shape
    {
        QcBlockPos head;
        QcBlockPos foot;
        std::array<QcBlockPos, 5> cells{};
    };

    QcServerGolems(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory);

    // A player placed a block at `pos`: a golem whose head it is, standing on a complete body,
    // comes alive. Returns the new golem's entity id, if one came.
    std::optional<quint32> build(QcBlockPos pos);
    // The player uses the held item on the creature: a golem it repairs is healed. False when it
    // is no repair (other uses may follow).
    bool repair(QcServerClient &client, quint32 entityId, const QcItemDefinition *held);

    // The T of `definition`'s body blocks under its head at `head`, if complete (the head itself is
    // not checked).
    static std::optional<Shape> shapeAt(const QcBlockAccess &world, QcBlockPos head, const QcMobDefinition &definition);

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
};
