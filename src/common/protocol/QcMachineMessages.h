#pragma once

// Protocol messages of machines (furnaces) and storage blocks (chests).
// Part of the QcMessage variant (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

namespace QcMsg {

// Server -> client: the player opened the machine at `pos`, or with an empty category the storage
// block (chest) there; MachineState follows.
struct OpenMachine
{
    QcBlockPos pos;
    QString category;
    // A container minecart (entity id, decision E-386) instead of the block at `pos`; 0 for a block.
    quint32 entity = 0;
};

// Server -> client: contents and progress of the open machine. `stacks` are input, fuel and output;
// `burn` is the share of the current fuel left and `progress` the share of the current item done.
// For a storage block, `stacks` are its slots and the bars stay at 0.
struct MachineState
{
    static constexpr int slotCount = 3;
    static constexpr int maxSlots = 54;

    QcBlockPos pos;
    // The container minecart of the screen (OpenMachine::entity); 0 for a block.
    quint32 entity = 0;
    QList<QcItemStack> stacks;
    float burn = 0.0f;
    float progress = 0.0f;
};

// Client -> server: the machine screen closed. Server -> client: the machine closed (out of reach,
// broken).
struct CloseMachine
{
};

} // namespace QcMsg
