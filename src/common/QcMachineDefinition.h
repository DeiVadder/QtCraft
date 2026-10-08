#pragma once

#include <QString>

// Block capability of a machine (furnace): an input, a fuel and an output slot that work through
// the smelting recipes of `category` while fuel burns.
struct QcMachineDefinition
{
    // Recipe category this machine processes; empty for blocks that are not machines.
    QString category;
    // Work done per second of burning, relative to a recipe's `seconds`; fuel burns at the same rate.
    double speed = 1.0;

    bool isMachine() const { return !category.isEmpty(); }
};
