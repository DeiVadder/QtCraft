#pragma once

#include "QcPistonPlanner.h"
#include "QcSignalSchedule.h"

#include <QHash>
#include <QList>

// What one simultaneous step of the circuits decided (decision E-335). Everything is decided on the
// world as it was before the step and applied together afterwards, so components that react in the
// same step never see each other's new state and their order does not matter.
struct QcCircuitPlan
{
    struct Schedule
    {
        QcBlockPos pos;
        int delay = 1;
        QcSignalSchedule::Priority priority = QcSignalSchedule::Priority::Diode;
    };

    struct Opening
    {
        QcBlockPos pos;
        bool open = false;
    };

    // New states of single blocks, at most one per cell (dust, torches, repeaters, lamps, switches).
    QHash<QcBlockPos, QcBlockId> writes;
    // Doors, trapdoors and gates to open or close (both halves of a door follow).
    QList<Opening> openings;
    QList<QcPistonPlan> pistons;
    QList<Schedule> schedules;
    // Torches that just burnt out (they fizzle).
    QList<QcBlockPos> burntOut;
    // Dispensers and droppers whose delay ended: they eject an item (decision E-345).
    QList<QcBlockPos> fires;
    // Work done for the tick's budget.
    qsizetype work = 0;

    bool isEmpty() const { return writes.isEmpty() && openings.isEmpty() && pistons.isEmpty() && schedules.isEmpty() && fires.isEmpty(); }
};
