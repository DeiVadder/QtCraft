#pragma once

// Protocol messages of machines (furnaces), storage blocks (chests) and the stonecutter.
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

// Server -> client: the player opened the stonecutter at `pos` (decision E-785). The screen lists
// what the stonecutting recipes of the content make from the stone blocks in the inventory.
struct OpenStonecutter
{
    QcBlockPos pos;
};

// Client -> server: cut the input of the stonecutting recipe `recipe` once, or with `repeat` as often
// as the inventory holds the input and has room (shift-click), at the open stonecutter.
struct CutStone
{
    static constexpr int maxRepeats = 64;

    QString recipe;
    bool repeat = false;
};

// Client -> server: the stonecutter screen closed. Server -> client: the stonecutter closed (broken,
// out of reach).
struct CloseStonecutter
{
};

} // namespace QcMsg
