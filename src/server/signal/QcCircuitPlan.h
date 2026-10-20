#pragma once

#include "server/signal/QcPistonPlanner.h"
#include "server/signal/QcSignalSchedule.h"

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
    // Explosives that got power: they are lit (decision E-601).
    QList<QcBlockPos> ignitions;
    // Note blocks whose power rose: they play their note (decision E-767).
    QList<QcBlockPos> notes;
    // Work done for the tick's budget.
    qsizetype work = 0;

    bool isEmpty() const { return writes.isEmpty() && openings.isEmpty() && pistons.isEmpty() && schedules.isEmpty() && fires.isEmpty() && ignitions.isEmpty() && notes.isEmpty(); }

    // What the components decide, in the words of the plan.
    void write(QcBlockPos pos, QcBlockId block) { writes.insert(pos, block); }
    void schedule(QcBlockPos pos, int delay, QcSignalSchedule::Priority priority) { schedules.append({pos, delay, priority}); }
    void open(QcBlockPos pos, bool isOpen) { openings.append({pos, isOpen}); }
    void movePiston(QcPistonPlan piston) { pistons.append(std::move(piston)); }
    void burnOut(QcBlockPos pos) { burntOut.append(pos); }
    void fire(QcBlockPos pos) { fires.append(pos); }
    void ignite(QcBlockPos pos) { ignitions.append(pos); }
    void playNote(QcBlockPos pos) { notes.append(pos); }
    void addWork(qsizetype amount) { work += amount; }
};
