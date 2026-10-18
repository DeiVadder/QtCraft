#pragma once

#include "common/core/QcVec3.h"

class QcPlayerHealth;
class QcServerSounds;
class QcServerState;
struct QcCreature;
struct QcEntity;
struct QcMobDefinition;

// What the world does to bodies by itself (decision E-367): blocks with contact damage (lava, fire,
// magma) hurt players and creatures and burn items, and players and creatures with their eyes in a
// full solid block suffocate. The void is QcPlayerHealth's (it has its own rhythm, E-288).
class QcEnvironmentDamage
{
public:
    // A player or creature whose eyes are inside a full solid block (a piston pushed it into a wall)
    // takes this much every interval, as in the reference (decision E-337).
    static constexpr double suffocationDamage = 1.0;
    static constexpr int suffocationIntervalTicks = 10;
    // A body touching a damaging block is hurt once every this many ticks, spread over the ticks by
    // its id, as the reference's lava and cactus are with its default invulnerability. The rhythm
    // is its own, so the game rules playerInvulnerabilityTicks and mobInvulnerabilityTicks govern
    // only how a hit takes (decision E-429), not how often lava strikes.
    static constexpr int contactIntervalTicks = 10;

    QcEnvironmentDamage(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    // Players and creatures touching a damaging block get hurt; items in it burn.
    void applyContactDamage();
    // Players and creatures with their eyes inside a full solid block suffocate.
    void applySuffocation();
    // Where a creature's eyes are: a standing share of its height above its feet; a villager lying
    // in its bed (decision E-411) has them in the middle of its lying box, inside the bed's cells,
    // so a bed under a low roof does not suffocate it (decision E-415).
    static QcVec3 eyesOf(const QcCreature &mob, const QcMobDefinition &definition);

private:
    // Whether contact damage falls due in this tick for the body.
    bool contactDue(const QcEntity &body) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
};
