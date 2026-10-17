#pragma once

#include "server/blocks/QcFireQueue.h"
#include "server/blocks/QcFireRules.h"

#include <functional>

class QcServerBlocks;
class QcServerSounds;
class QcServerState;

// Fire in the server tick (decision E-392). The fires due in QcFireQueue take a step each, at most
// QcServerLimits::fireStepsPerTick (the rest waits for the next tick): a fire that can no longer
// stand goes out, an eternal one (on a block with "eternalFire", outside the caps, decision E-394)
// only waits for its next step; any
// other fire ages by one and goes out at QcFireQueue::maxAge, or earlier without a flammable block
// next to it. While fire spreads (game rule fireSpread and limit fireSpreadRadius above 0), a step
// may burn away the flammable blocks next to the fire (they become fire or air) and light the air
// around it next to flammable blocks; only in loaded chunks, within fireSpreadRadius of the spark
// the fire came from, and while the chunk and the world have room for more fires. New fires start
// with the age of the fire that lit them, so a line of fires ends after QcFireQueue::maxAge steps
// whatever it finds. Rain (setRainCheck, decision E-396) puts out the fires it falls on, by a chance
// per step, and no new fire catches where it falls.
class QcServerFire
{
public:
    // Counts since the server started (tests, measurements).
    struct Numbers
    {
        quint64 steps = 0;
        quint64 lit = 0;
        quint64 burnt = 0;
        quint64 putOut = 0;
        // New fires refused because their chunk or the world had no room.
        quint64 refused = 0;
        // Fires put out by rain (also counted in putOut).
        quint64 rainedOut = 0;
    };

    // Whether rain falls on a position (weather, decision E-396); in each step of a fire it falls on,
    // rain puts the fire out with a chance that grows with the fire's age (eternal fires excepted).
    using RainCheck = std::function<bool(QcBlockPos pos)>;

    QcServerFire(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds);

    // Adopts the fire blocks set without an entry, then runs the due steps within the budget.
    void tick();
    // Lights `fire` (the content's default fire when unset) at `pos` as a new spark: only in air of
    // a loaded chunk where fire may stand and while there is room. For ignition sources.
    bool ignite(QcBlockPos pos, std::optional<QcBlockId> fire = std::nullopt);
    void setRainCheck(RainCheck check) { m_rainsOn = std::move(check); }

    // Whether fire spreads now: the game rule fireSpread and a fireSpreadRadius above 0.
    bool spreads() const;
    const Numbers &numbers() const { return m_numbers; }
    const QcFireRules::Table &table() const { return m_table; }

private:
    void adoptUnplanned(quint64 now);
    // Puts out the fire blocks the queue refused a place (QcFireQueue::takeRefused).
    void putOutRefused();
    // Whether the block below `pos` is eternal ground: fire there needs no room in the caps.
    bool onEternalGround(QcBlockPos pos) const;
    void step(QcFireQueue::Fire fire, quint64 now);
    // Whether rain falling on the fire puts it out in this step (by the dice of the step).
    bool rainPutsOut(const QcFireQueue::Fire &fire) const;
    // Burns the flammable blocks next to the fire away: each by its chance becomes fire or air.
    void burnNeighbours(const QcFireQueue::Fire &fire, QcBlockId block, quint64 now);
    // Lights the air around the fire next to flammable blocks, each cell by its chance.
    void spreadAround(const QcFireQueue::Fire &fire, QcBlockId block, quint64 now);
    // Whether the air at `pos` near the fire catches in this step (next to flammable blocks, within reach).
    bool catches(const QcFireQueue::Fire &fire, QcBlockPos pos) const;
    // Puts a new fire lit by `parent` at `pos`, if there is room and no rain falls there.
    bool light(QcBlockPos pos, const QcFireQueue::Fire &parent, QcBlockId block, quint64 now);
    void burnAway(QcBlockPos pos, QcBlockId burnt);
    void putOut(QcBlockPos pos);
    int reach() const;
    QcFireRules::Dice dice(QcBlockPos fire, int age) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    QcFireRules::Table m_table;
    RainCheck m_rainsOn;
    Numbers m_numbers;
    // Burning blocks make at most one sound per tick.
    bool m_soundPlayed = false;
};
