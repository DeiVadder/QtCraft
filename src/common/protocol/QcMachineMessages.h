#pragma once

// Protocol messages of machines (furnaces) and storage blocks (chests).
// Part of the QcMessage variant (QcMessages.h).

#include "QcBlockPos.h"
#include "QcItemStack.h"

#include <QList>
#include <QString>

namespace QcMsg {

// Server -> client: the player opened the machine at `pos`, or with an empty category the storage
// block (chest) there; MachineState follows.
struct OpenMachine
{
    QcBlockPos pos;
    QString category;
};

// Server -> client: contents and progress of the open machine. `stacks` are input, fuel and output;
// `burn` is the share of the current fuel left and `progress` the share of the current item done.
// For a storage block, `stacks` are its slots and the bars stay at 0.
struct MachineState
{
    static constexpr int slotCount = 3;
    static constexpr int maxSlots = 54;

    QcBlockPos pos;
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
