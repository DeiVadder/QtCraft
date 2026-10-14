#pragma once

class QcPlayerHealth;
class QcServerSounds;
class QcServerState;

// The creatures in the server tick (decision E-367): their behaviour and spawning (QcMobManager) with
// what they need to know about the players, the blows they deal to players and their ambient sounds.
class QcServerCreatures
{
public:
    // A creature with an ambient sound makes it on average this often (seconds).
    static constexpr double ambientSeconds = 10.0;

    QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    void tick();

private:
    void playAmbientSounds(double dt);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
};
