#pragma once

class QcPlayerHealth;
class QcServerSounds;
class QcServerState;

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

    QcEnvironmentDamage(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    // Players and creatures touching a damaging block get hurt; items in it burn.
    void applyContactDamage();
    // Players and creatures with their eyes inside a full solid block suffocate.
    void applySuffocation();

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
};
