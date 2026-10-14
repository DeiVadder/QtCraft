#pragma once

#include "QcBlockEntities.h"
#include "QcSignalSchedule.h"

#include <QList>

#include <optional>

// What is saved of the block entities of one chunk (decision E-358), with every tick counted from
// the moment of saving, so clocks go on where they stopped when the world opens again.
struct QcBlockEntityRecord
{
    // What a circuit component remembers; torches' recent switching is not saved.
    struct Memory
    {
        QcBlockPos pos;
        // Ticks until a burnt-out torch may light again; 0: not burnt out.
        quint32 burntOutLeft = 0;
        bool powered = false;
        // Ticks since a piston's extension started.
        std::optional<quint32> extendingAge;
    };

    struct Scheduled
    {
        QcBlockPos pos;
        quint32 ticksLeft = 0;
        QcSignalSchedule::Priority priority = QcSignalSchedule::Priority::Diode;
    };

    QcChunkPos chunk;
    // Containers and machines with contents (or a cooldown); empty ones come back with their block.
    // A container's `readyAt` holds the ticks left of a hopper's cooldown.
    QList<QcBlockEntities::Container> containers;
    QList<QcBlockEntities::Machine> machines;
    QList<QcBlockEntities::Sapling> saplings;
    QList<Memory> memories;
    QList<Scheduled> scheduled;
    // Positions the circuits were going to look at (changes not yet looked at, positions waiting for
    // chunks): looked at as if just placed once the chunk and the eight around it are loaded.
    QList<QcBlockPos> wakes;

    bool isEmpty() const
    {
        return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty() && scheduled.isEmpty()
               && wakes.isEmpty();
    }
};
