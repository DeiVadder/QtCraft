#pragma once

#include "QcCircuitComponents.h"

// How the components of stage 29.2 react, as decisions on the world before a step like
// QcCircuitComponents (decisions E-343 to E-345):
// - a Signal Comparator recomputes its output 2 ticks after its inputs changed: the back input (or
//   the reading of a container or readable block behind it, also through one conductor) compared
//   with or reduced by the strongest side input;
// - a Signal Observer turns on 2 ticks after the block in front of it changed and off 2 ticks later;
// - a Signal Hopper is locked while it gets any power;
// - a Signal Dispenser or dropper notes rising power (also quasi-connectivity, like a piston) and
//   ejects an item 4 ticks later.
class QcCircuitMachines
{
public:
    static constexpr int comparatorDelayTicks = 2;
    static constexpr int observerPulseTicks = 2;
    static constexpr int dispenserDelayTicks = 4;

    QcCircuitMachines(const QcCircuitComponents &components, const QcCircuitComponents::Context &context);

    void reactComparator(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void switchComparator(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    // The strength the comparator at `pos` would put out now.
    int comparatorOutput(QcBlockPos pos, QcSignalState state) const;

    void reactObserver(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void observed(QcBlockPos pos, QcCircuitPlan &plan) const;
    void switchObserver(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;

    void reactHopper(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;

    void reactDispenser(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;

private:
    // The back input: power from behind, replaced by the reading of a readable block behind it or,
    // when a conductor that gets less than 15 is behind it, two behind it.
    int backInput(QcBlockPos pos, QcBlockFace back) const;
    // A side input: dust's strength, a diode pointing in, or a source's strong power towards it.
    int sideInput(QcBlockPos pos, QcBlockFace side) const;

    const QcSignalTable &table() const { return m_context.power.table(); }

    const QcCircuitComponents &m_components;
    const QcCircuitComponents::Context &m_context;
};
