#pragma once

#include "QcCircuitComponents.h"
#include "QcCircuitPlan.h"
#include "QcCircuitState.h"
#include "QcDispenserActions.h"
#include "QcServerHoppers.h"
#include "QcPistonMover.h"
#include "QcRails.h"
#include "QcSignalTable.h"

#include <QHash>
#include <QSet>

#include <vector>

class QcServerBlocks;
class QcServerSounds;
class QcServerState;

// The Signal Dust circuits of the server: the tick phase "signal" (decision E-335). Each tick it
// presses plates, then looks at the block changes near circuits since the last step and at the
// scheduled ticks that are due, in steps: a step decides everything on the world as it is
// (dust networks in one pass, components, pistons) and applies it together; the changes it makes
// are the next step. Steps run until the circuits are quiet or the tick's budget is used up; what
// is left waits for the next tick, so a runaway circuit never stalls the server.
class QcServerCircuits
{
public:
    // The budget of work per tick (dust looked at, components evaluated, blocks pushed, bodies looked
    // at), the share kept for due scheduled ticks, the work a change counts for and the least changes
    // a step takes come from QcServerLimits (decision E-349). A step takes at most what is left
    // (split in a fixed order of positions); a dust region or a piston's push in it can still cross
    // it, nothing starts after that.
    // A signal door that a body kept from moving tries again after this many ticks.
    static constexpr int openerRetryTicks = 10;
    // Memories of torches, burnouts and extensions that ran out are dropped this often.
    static constexpr int pruneIntervalTicks = 100;

    QcServerCircuits(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    void tick();
    // A player uses the block at `pos`: a lever switches, a button is pressed, a repeater takes its
    // next delay. False when it is no such block.
    bool use(QcBlockPos pos);
    // Makes the circuits around `pos` look at it in the next step, as if it had just been placed
    // (world setup, chunks generated again).
    void noteBlock(QcBlockPos pos);

    const QcSignalTable &table() const { return m_table; }
    const QcServerHoppers &hoppers() const { return m_hoppers; }
    // How a dispenser puts armour on a player (the server's inventory part does it).
    void setEquipPlayer(QcDispenserActions::EquipPlayer equip) { m_dispensers.setEquipPlayer(std::move(equip)); }
    void setIgnite(QcDispenserActions::Ignite ignite) { m_dispensers.setIgnite(std::move(ignite)); }
    // Work done in the latest tick (debug display).
    qsizetype lastTickWork() const { return m_lastWork; }

private:
    void finishMoves();
    void pressPlates();
    // Comparators behind detector rails read again where the container cart they read changed
    // (QcComparatorReading::Carts): another cart arrived or left while the rail stayed pressed.
    void noteCartReadings(QHash<QcBlockPos, quint32> readings);
    // The bodies that press the plate `plate` in this tick (QcSignalDefinition::detectsItems, detectsCarts).
    const QSet<QcBlockPos> &pressedSet(QcBlockId plate) const;
    // Runs steps of changes while more than `reserve` of the budget is left.
    void settle(qsizetype reserve);
    // The changes of the next step, at most `limit` (at least the minimum step), without those next to
    // an unloaded chunk (they wait for it).
    QList<QcCircuitState::Change> takeChanges(qsizetype limit);
    void runWave(qsizetype limit);
    void runScheduled(quint64 due, QcSignalSchedule::Priority priority);
    // What a set of changes may concern, sorted, each once: components next to them, dust around
    // them, comparators reading changed contents and observers looking at changed blocks.
    struct Collected
    {
        std::vector<QcBlockPos> components;
        std::vector<QcBlockPos> dust;
        std::vector<QcBlockPos> readers;
        std::vector<QcBlockPos> observers;
    };
    void collect(const QList<QcCircuitState::Change> &changes, Collected &collected) const;
    void collectReaders(QcBlockPos pos, std::vector<QcBlockPos> &readers) const;
    void collectObservers(QcBlockPos pos, std::vector<QcBlockPos> &observers) const;
    void reactReadersAndObservers(const Collected &collected, QcCircuitComponents &reactions, QcCircuitPlan &plan) const;
    void collectAround(const QcCircuitState::Change &change, std::vector<QcBlockPos> &components, std::vector<QcBlockPos> &dust) const;
    void apply(QcCircuitPlan &plan);
    void applyWrites(const QcCircuitPlan &plan);
    void applyOpenings(const QcCircuitPlan &plan);
    // Returns the work done (blocks moved, bodies looked at).
    qsizetype applyPistons(const QcCircuitPlan &plan);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcSignalTable m_table;
    QcPistonMover m_mover;
    QcDispenserActions m_dispensers;
    QcServerHoppers m_hoppers;
    qsizetype m_budget = 0;
    qsizetype m_lastWork = 0;
    // Pistons that extended (bit 1) or retracted (bit 2) in this tick: each does both at most once.
    QHash<QcBlockPos, quint8> m_pistonActs;
    QSet<QcBlockPos> m_pressedByAnything;
    QSet<QcBlockPos> m_pressedByLiving;
    QSet<QcBlockPos> m_pressedByCarts;
    // The container cart with the lowest id in each plate cell in the latest tick (what a comparator reads).
    QHash<QcBlockPos, quint32> m_cartReadings;
    QcRails::Table m_rails;
};
