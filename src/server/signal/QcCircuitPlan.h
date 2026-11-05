// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/signal/QcPistonPlanner.h"
#include "server/signal/QcSignalSchedule.h"

#include <QHash>
#include <QList>

/*! \brief What one simultaneous step of the circuits decided (decision E-335).

    Everything is decided on the world as it was before the step and applied together afterwards,
    so components that react in the same step never see each other's new state and their order
    does not matter. */
struct QcCircuitPlan
{
    /*! \brief A scheduled tick to plan: the component at \c {pos} ticks after \c {delay} ticks
        with \c {priority}. \qcfields */
    struct Schedule
    {
        QcBlockPos pos;
        int delay = 1;
        QcSignalSchedule::Priority priority = QcSignalSchedule::Priority::Diode;
    };

    /*! \brief A door, trapdoor or gate at \c {pos} to set \c {open} or closed. \qcfields */
    struct Opening
    {
        QcBlockPos pos;
        bool open = false;
    };

    /*! New states of single blocks, at most one per cell (dust, torches, repeaters, lamps,
        switches). */
    QHash<QcBlockPos, QcBlockId> writes;
    /*! Doors, trapdoors and gates to open or close (both halves of a door follow). */
    QList<Opening> openings;
    /*! Pistons to extend or retract. */
    QList<QcPistonPlan> pistons;
    /*! Scheduled ticks to plan. */
    QList<Schedule> schedules;
    /*! Torches that just burnt out (they fizzle). */
    QList<QcBlockPos> burntOut;
    /*! Dispensers and droppers whose delay ended: they eject an item (decision E-345). */
    QList<QcBlockPos> fires;
    /*! Explosives that got power: they are lit (decision E-601). */
    QList<QcBlockPos> ignitions;
    /*! Note blocks whose power rose: they play their note (decision E-767). */
    QList<QcBlockPos> notes;
    /*! Work done for the tick's budget. */
    qsizetype work = 0;

    /*! Returns whether the step decided nothing to apply. */
    bool isEmpty() const { return writes.isEmpty() && openings.isEmpty() && pistons.isEmpty() && schedules.isEmpty() && fires.isEmpty() && ignitions.isEmpty() && notes.isEmpty(); }

    /*! Plans the new state \a block at \a pos (what the components decide, in the words of the
        plan). */
    void write(QcBlockPos pos, QcBlockId block) { writes.insert(pos, block); }
    /*! Plans a tick of the component at \a pos after \a delay ticks with \a priority. */
    void schedule(QcBlockPos pos, int delay, QcSignalSchedule::Priority priority) { schedules.append({pos, delay, priority}); }
    /*! Plans to open (\a isOpen) or close the door, trapdoor or gate at \a pos. */
    void open(QcBlockPos pos, bool isOpen) { openings.append({pos, isOpen}); }
    /*! Plans the move of \a piston. */
    void movePiston(QcPistonPlan piston) { pistons.append(std::move(piston)); }
    /*! Notes that the torch at \a pos burnt out. */
    void burnOut(QcBlockPos pos) { burntOut.append(pos); }
    /*! Plans the dispenser or dropper at \a pos to eject an item. */
    void fire(QcBlockPos pos) { fires.append(pos); }
    /*! Plans the explosive at \a pos to be lit. */
    void ignite(QcBlockPos pos) { ignitions.append(pos); }
    /*! Plans the note block at \a pos to play its note. */
    void playNote(QcBlockPos pos) { notes.append(pos); }
    /*! Adds \a amount to the work done. */
    void addWork(qsizetype amount) { work += amount; }
};
