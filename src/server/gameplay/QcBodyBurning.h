#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <functional>

class QcPlayerHealth;
class QcServerSounds;
struct QcEntity;
struct QcServerClient;
class QcServerState;

// Players and creatures on fire (decision E-393). A body touching a block with `setsAlight` (fire
// 8 s, lava 15 s; QcContactDamage::touching) burns for at least that long from then on; while it
// burns it takes burnDamage once every burnIntervalTicks (players through their armour, like the
// contact damage of the block), and water around its feet or body puts it out at once, as will rain
// (setRainCheck, weather of stage 35). Players in creative mode never catch fire. Players hear of
// their own burning with their health (QcMsg::PlayerHealth::burning), everyone sees it on the
// entity (QcMsg::Burning). Creatures that burn in daylight catch fire in direct sunlight. A burning creature makes its hurt sound when the damage takes and drops
// its loot when it dies, like after a hit. Items in fire or lava burn up in QcEnvironmentDamage.
// The same water and rain hurt creatures with `waterDamage` (endermen, decision E-460) once every
// wetIntervalTicks, and one that teleports jumps away from it.
class QcBodyBurning
{
public:
    static constexpr double burnDamage = 1.0;
    static constexpr int burnIntervalTicks = 20;
    // Creatures that burn in daylight (QcMobDefinition::burnsInDaylight, decision E-426) catch fire
    // for this long in direct sunlight, as from fire; water, rain and shade keep them from it.
    static constexpr int sunlightTicks = 8 * 20;
    // Creatures hurt by water (QcMobDefinition::waterDamage) take it this often while wet.
    static constexpr int wetIntervalTicks = 10;

    // Whether rain falls on a position; rain puts out the bodies it falls on.
    using RainCheck = std::function<bool(QcBlockPos pos)>;

    QcBodyBurning(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    // One simulated tick of every burning or touching player and creature.
    void tick();
    void setRainCheck(RainCheck check) { m_rainsOn = std::move(check); }

private:
    // The burn time a body has after this tick: what it touches sets it alight, water or rain puts
    // it out, otherwise one tick less.
    int burnTicksAfter(const QcEntity &body, double width, double height, int alightTicks = 0) const;
    // Whether the sun shines on the head of a body of `height` (decision E-426): day, full sky light.
    bool inSunlight(const QcEntity &body, double height) const;
    // Whether water (a liquid that sets nothing alight) fills a cell of the body's feet or middle.
    bool inWater(const QcVec3 &feet, double height) const;
    // Whether rain falls on the body standing at `feet` (the rain check on the cell above the feet).
    bool rainsOn(const QcVec3 &feet) const;
    // Whether the burn damage falls due in this tick for the body (spread over the ticks by id).
    bool damageDue(const QcEntity &body) const;
    void tickPlayer(QcServerClient &client);
    void tickCreatures();
    // Creatures hurt by water take their damage while wet and teleport away.
    void tickWetCreatures();
    // Water creatures out of water longer than their air suffocate once a second (decision E-502).
    void tickDryCreatures();
    // Hurts a creature like a hit (sounds, loot at its death).
    void hurtCreature(quint32 id, double amount);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
    RainCheck m_rainsOn;
};
