#pragma once

#include "QcCircuitPlan.h"
#include "QcCircuitState.h"
#include "QcSignalPower.h"

#include <QSet>

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
// - buttons spring back after `pulseTicks`, plates when nothing stands on them any more.
class QcCircuitComponents
{
public:
    static constexpr int torchDelayTicks = 2;
    static constexpr int burnoutToggles = 8;
    static constexpr int burnoutWindowTicks = 60;
    static constexpr int burnoutTicks = 160;
    static constexpr int repeaterStepTicks = 2;
    static constexpr int plateCheckTicks = 20;

    struct Context
    {
        const QcSignalPower &power;
        QcCircuitState &memory;
        quint64 now = 0;
        // The world's game rule; each piston can also refuse it in data.
        bool quasiConnectivity = true;
        // Pistons that already extended (bit 1) or retracted (bit 2) in this tick.
        const QHash<QcBlockPos, quint8> &pistonActs;
        // Plates something lies on in this tick, and those a player or creature stands on.
        const QSet<QcBlockPos> &pressedByAnything;
        const QSet<QcBlockPos> &pressedByLiving;
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

private:
    void reactTorch(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void switchTorch(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan);
    bool torchShouldLight(QcBlockPos pos, QcSignalState state) const;
    void reactRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void switchRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    bool repeaterLocked(QcBlockPos pos, QcSignalState state) const;
    void reactLamp(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactOpener(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan);
    void reactPiston(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;
    void reactHead(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const;
    void releasePlate(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const;

    const QcSignalTable &table() const { return m_context.power.table(); }
    const QcSignalDefinition &definition(QcBlockId block) const;

    Context m_context;
};
