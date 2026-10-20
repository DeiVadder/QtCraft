#pragma once

#include "server/blocks/QcRails.h"
#include "server/signal/QcCircuitComponents.h"
#include "server/signal/QcCircuitPlan.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcDispenserActions.h"
#include "server/signal/QcNoteBudget.h"
#include "server/signal/QcPistonMover.h"
#include "server/signal/QcServerHoppers.h"
#include "server/signal/QcSignalTable.h"

#include <QHash>

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
    // next delay, a note block its next note (and plays it). False when it is no such block.
    bool use(QcBlockPos pos);
    // A projectile hit the target block at `pos` with `power` (1..15, decision E-768); `stuck` for
    // projectiles that stick (arrows), which hold it longer. A new hit replaces the strength and
    // starts the time again. False when it is no target.
    bool hitTarget(QcBlockPos pos, int power, bool stuck);
    // The strength of a hit at `point` (world space) on the face `face` of the block at `pos`: 15 in
    // the middle, falling evenly to 1 at the edges (the larger distance along the face's two axes,
    // as in the reference).
    static int targetStrength(QcBlockPos pos, QcBlockFace face, const QcVec3 &point);
    // Plays the note of the note block at `pos` (decision E-767): its instrument comes from the block
    // below, its pitch from the state `note`; nothing sounds while a block lies on top or while the
    // note budget (QcNoteBudget) holds the block back.
    void playNote(QcBlockPos pos);
    // Makes the circuits around `pos` look at it in the next step, as if it had just been placed
    // (world setup, chunks generated again).
    void noteBlock(QcBlockPos pos);

    const QcSignalTable &table() const { return m_table; }
    const QcServerHoppers &hoppers() const { return m_hoppers; }
    // How a dispenser puts armour on a player (the server's inventory part does it).
    void setEquipPlayer(QcDispenserActions::EquipPlayer equip) { m_dispensers.setEquipPlayer(std::move(equip)); }
    void setIgnite(QcDispenserActions::Ignite ignite) { m_dispensers.setIgnite(std::move(ignite)); }
    // How a powered igniter lights its explosive (QcServerExplosions::light, decision E-601).
    void setLight(QcDispenserActions::Ignite light) { m_light = std::move(light); }
    // Work done in the latest tick (debug display).
    qsizetype lastTickWork() const { return m_lastWork; }

private:
    void finishMoves();
    void pressPlates();
    // The plates and tripwire something pressed in this tick that were not pressed yet.
    [[nodiscard]] QcCircuitPlan pressedPlan() const;
    // Comparators behind detector rails read again where the container cart they read changed
    // (QcComparatorReading::Carts): another cart arrived or left while the rail stayed pressed.
    void noteCartReadings(QHash<QcBlockPos, quint32> readings);
    // What the components look at in a step, on `power` and `readings` of the world as it is.
    QcCircuitComponents::Context componentContext(QcSignalPower power, const QcComparatorReadings &readings) const;
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

        // The components and dust that `change` may concern.
        void addAround(const QcCircuitState::Change &change, const QcSignalTable &table);
        // The observers looking at `pos`.
        void addObservers(QcBlockPos pos, const QcServerWorld &world, const QcSignalTable &table);
        // The comparators reading `pos`: behind it, or two behind it through a conductor.
        void addReaders(QcBlockPos pos, const QcServerWorld &world);
        // Sorts every list and keeps each position once.
        void sortUnique();
    };
    [[nodiscard]] Collected collect(const QList<QcCircuitState::Change> &changes) const;
    void reactReadersAndObservers(const Collected &collected, QcCircuitComponents &reactions, QcCircuitPlan &plan) const;
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
    QcDispenserActions::Ignite m_light;
    QcServerHoppers m_hoppers;
    qsizetype m_budget = 0;
    qsizetype m_lastWork = 0;
    // Pistons that extended (bit 1) or retracted (bit 2) in this tick: each does both at most once.
    QHash<QcBlockPos, quint8> m_pistonActs;
    QcCircuitComponents::Plates m_plates;
    QcNoteBudget m_notes;
    // The container cart with the lowest id in each plate cell in the latest tick (what a comparator reads).
    QHash<QcBlockPos, quint32> m_cartReadings;
    QcRails::Table m_rails;
};
