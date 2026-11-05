// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/blockrules/QcRails.h"
#include "server/blockentities/QcBlockEntities.h"
#include "server/signal/QcCircuitPlan.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcComparatorReadings.h"
#include "server/signal/QcSignalPower.h"

#include <QSet>

struct QcSignalDefinition;

/*! \brief How each kind of component reacts (decision E-336), as decisions on the world before a
    step.

    A component reacts when a block changes next to it (or next to what powers it) and again when
    its scheduled tick comes:

    \list
    \li a Signal Torch goes out or lights 2 ticks after its block's power changed; going out 8 times
        within 60 ticks burns it out for 160 ticks (it fizzles);
    \li a Signal Repeater switches after 2 ticks per delay step and always gives a pulse of at least
        its delay; a powered repeater pointing at its side locks it;
    \li a Signal Lamp lights at once and goes dark \c {offDelayTicks} after losing power;
    \li an opener follows rising and falling power, so players can still use it in between;
    \li a piston extends or retracts at once (BUD: with quasi-connectivity it is also powered
        through the block above it, but only finds out when something next to it changes);
    \li buttons spring back after \c {pulseTicks}, plates when nothing stands on them any more;
    \li a Signal Comparator recomputes its output 2 ticks after its inputs changed: the back input
        (or the reading of a container or readable block behind it, also through one conductor)
        compared with or reduced by the strongest side input (decision E-343);
    \li a Signal Observer turns on 2 ticks after the block in front of it changed and off 2 ticks
        later;
    \li a Signal Hopper is locked while it gets any power;
    \li a Signal Dispenser or dropper notes rising power (also quasi-connectivity, like a piston)
        and ejects an item 4 ticks later (decision E-345);
    \li a rail slot (powered and activator rails) is powered at once while it or a rail of the same
        block within its reach along the line gets power; a change at one rail decides all rails
        within reach of it (decision E-384).
    \endlist */
class QcCircuitComponents
{
public:
    /*! The timings of the components in ticks, as listed above: torch delay, burnout after
        \c {burnoutToggles} times going out within \c {burnoutWindowTicks} for
        \c {burnoutTicks}, a repeater's step, the check of a plate, the comparator's delay, the
        observer's pulse and the dispenser's delay. \qcshared */
    static constexpr int torchDelayTicks = 2;
    static constexpr int burnoutToggles = 8;
    static constexpr int burnoutWindowTicks = 60;
    static constexpr int burnoutTicks = 160;
    static constexpr int repeaterStepTicks = 2;
    static constexpr int plateCheckTicks = 20;
    static constexpr int comparatorDelayTicks = 2;
    static constexpr int observerPulseTicks = 2;
    static constexpr int dispenserDelayTicks = 4;

    /*! \brief Plates something lies on in this tick, those a player or creature stands on and
        those a minecart stands on; tripwire something touches (decision E-782). \qcfields */
    struct Plates
    {
        QSet<QcBlockPos> anything;
        QSet<QcBlockPos> living;
        QSet<QcBlockPos> carts;
        QSet<QcBlockPos> wires;

        /*! Returns the plates of this tick that a plate with \a signal counts as pressed
            (QcSignalDefinition::detectsItems, detectsCarts). */
        const QSet<QcBlockPos> &pressing(const QcSignalDefinition &signal) const;
        /*! Forgets every plate and wire. */
        void clear();
    };

    /*! \brief What the components decide on: the world before the step and what the tick
        knows. */
    struct Context
    {
        /*! A view of the world and the signal table, copied. */
        QcSignalPower power;
        /*! What components remember at their position and their scheduled ticks. */
        QcBlockEntities &entities;
        /*! The current tick. */
        quint64 now = 0;
        /*! The world's game rule; each piston can also refuse it in data. */
        bool quasiConnectivity = true;
        /*! Pistons that already extended (bit 1) or retracted (bit 2) in this tick. */
        const QHash<QcBlockPos, quint8> &pistonActs;
        /*! What lies on plates and touches tripwire in this tick. */
        const Plates &plates;
        /*! What comparators read from containers and readable blocks. */
        const QcComparatorReadings &readings;
        /*! The tracks of rails (the lines of powered and activator rails). */
        const QcRails::Table &rails;
    };

    /*! The bits of Context::pistonActs. \qcshared */
    static constexpr quint8 extendedThisTick = 1;
    static constexpr quint8 retractedThisTick = 2;

    explicit QcCircuitComponents(const Context &context);

    /*! A block changed near the component at \a pos: its decisions go into \a plan. */
    void react(QcBlockPos pos, QcCircuitPlan &plan);
    /*! The scheduled tick of the component at \a pos came: its decisions go into \a plan. */
    void scheduled(QcBlockPos pos, QcCircuitPlan &plan);
    /*! Returns whether the piston at \a pos in \a state is powered now. */
    bool pistonPowered(QcBlockPos pos, QcSignalState state) const;
    /*! Returns whether the block at \a pos is powered through any face but those in
        \a ignoredFaces, or (with quasi-connectivity, the world's rule and the block's) through what
        would power the block above it. */
    bool poweredOrQuasi(QcBlockPos pos, quint8 ignoredFaces) const;
    /*! An observer at \a pos saw the block in front of it change: its pulse goes into \a plan. */
    void observed(QcBlockPos pos, QcCircuitPlan &plan) const;
    /*! The block \a before at \a pos became \a after: a tripwire hook or wire that is gone tells
        its lines in \a plan (decision E-782). */
    void replaced(QcBlockPos pos, QcBlockId before, QcBlockId after, QcCircuitPlan &plan) const;

private:
    void reactTorch(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void switchTorch(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan);
    bool torchShouldLight(QcBlockPos pos, QcSignalState state) const;
    void reactRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void switchRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    bool repeaterLocked(QcBlockPos pos, QcSignalState state) const;
    void reactLamp(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactNote(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactOpener(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan);
    void reactPiston(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactHead(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void releasePlate(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    // Scheduled ticks of comparators, observers and dispensers.
    void scheduledMachine(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactComparator(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void switchComparator(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    // The strength the comparator at `pos` would put out now.
    int comparatorOutput(QcBlockPos pos, QcSignalState state) const;
    // The back input: power from behind, replaced by the reading of a readable block behind it or,
    // when a conductor that gets less than 15 is behind it, two behind it.
    int backInput(QcBlockPos pos, QcBlockFace back) const;
    // A side input: dust's strength, a diode pointing in, or a source's strong power towards it.
    int sideInput(QcBlockPos pos, QcBlockFace side) const;
    void reactObserver(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void observedChange(QcBlockPos pos, QcCircuitPlan &plan) const;
    void switchObserver(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactHopper(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactDispenser(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    // Decides every rail of the line within reach of the rail at `pos`.
    void reactRail(QcBlockPos pos, QcBlockId block, QcCircuitPlan &plan) const;

    const QcSignalTable &table() const { return m_context.power.table(); }
    const QcSignalDefinition &definition(QcBlockId block) const;

    Context m_context;
};
