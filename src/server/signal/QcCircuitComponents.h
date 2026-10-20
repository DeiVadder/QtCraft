#pragma once

#include "server/blockentities/QcBlockEntities.h"
#include "server/blocks/QcRails.h"
#include "server/signal/QcCircuitPlan.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcComparatorReadings.h"
#include "server/signal/QcSignalPower.h"

#include <QSet>

struct QcSignalDefinition;

// How each kind of component reacts (decision E-336), as decisions on the world before a step. A
// component reacts when a block changes next to it (or next to what powers it) and again when its
// scheduled tick comes:
// - a Signal Torch goes out or lights 2 ticks after its block's power changed; going out 8 times
//   within 60 ticks burns it out for 160 ticks (it fizzles);
// - a Signal Repeater switches after 2 ticks per delay step and always gives a pulse of at least its
//   delay; a powered repeater pointing at its side locks it;
// - a Signal Lamp lights at once and goes dark `offDelayTicks` after losing power;
// - an opener follows rising and falling power, so players can still use it in between;
// - a piston extends or retracts at once (BUD: with quasi-connectivity it is also powered through
//   the block above it, but only finds out when something next to it changes);
// - buttons spring back after `pulseTicks`, plates when nothing stands on them any more;
// - a Signal Comparator recomputes its output 2 ticks after its inputs changed: the back input (or
//   the reading of a container or readable block behind it, also through one conductor) compared
//   with or reduced by the strongest side input (decision E-343);
// - a Signal Observer turns on 2 ticks after the block in front of it changed and off 2 ticks later;
// - a Signal Hopper is locked while it gets any power;
// - a Signal Dispenser or dropper notes rising power (also quasi-connectivity, like a piston) and
//   ejects an item 4 ticks later (decision E-345);
// - a rail slot (powered and activator rails) is powered at once while it or a rail of the same block
//   within its reach along the line gets power; a change at one rail decides all rails within reach
//   of it (decision E-384).
class QcCircuitComponents
{
public:
    static constexpr int torchDelayTicks = 2;
    static constexpr int burnoutToggles = 8;
    static constexpr int burnoutWindowTicks = 60;
    static constexpr int burnoutTicks = 160;
    static constexpr int repeaterStepTicks = 2;
    static constexpr int plateCheckTicks = 20;
    static constexpr int comparatorDelayTicks = 2;
    static constexpr int observerPulseTicks = 2;
    static constexpr int dispenserDelayTicks = 4;

    // Plates something lies on in this tick, those a player or creature stands on and those a
    // minecart stands on.
    struct Plates
    {
        QSet<QcBlockPos> anything;
        QSet<QcBlockPos> living;
        QSet<QcBlockPos> carts;

        // The plates of this tick that a plate with `signal` counts as pressed
        // (QcSignalDefinition::detectsItems, detectsCarts).
        const QSet<QcBlockPos> &pressing(const QcSignalDefinition &signal) const;
        void clear();
    };

    struct Context
    {
        // A view of the world and the signal table, copied.
        QcSignalPower power;
        // What components remember at their position and their scheduled ticks.
        QcBlockEntities &entities;
        quint64 now = 0;
        // The world's game rule; each piston can also refuse it in data.
        bool quasiConnectivity = true;
        // Pistons that already extended (bit 1) or retracted (bit 2) in this tick.
        const QHash<QcBlockPos, quint8> &pistonActs;
        const Plates &plates;
        // What comparators read from containers and readable blocks.
        const QcComparatorReadings &readings;
        // The tracks of rails (the lines of powered and activator rails).
        const QcRails::Table &rails;
    };

    static constexpr quint8 extendedThisTick = 1;
    static constexpr quint8 retractedThisTick = 2;

    explicit QcCircuitComponents(const Context &context);

    // A block changed near the component at `pos`.
    void react(QcBlockPos pos, QcCircuitPlan &plan);
    // The scheduled tick of the component at `pos` came.
    void scheduled(QcBlockPos pos, QcCircuitPlan &plan);
    // Whether the piston at `pos` is powered now.
    bool pistonPowered(QcBlockPos pos, QcSignalState state) const;
    // Powered through any face but those in `ignoredFaces`, or (with quasi-connectivity, the world's
    // rule and the block's) through what would power the block above it.
    bool poweredOrQuasi(QcBlockPos pos, quint8 ignoredFaces) const;
    // An observer at `pos` saw the block in front of it change.
    void observed(QcBlockPos pos, QcCircuitPlan &plan) const;

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
