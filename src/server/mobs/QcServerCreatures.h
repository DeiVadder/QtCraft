#pragma once

#include "server/villages/QcServerVillagers.h"

class QcPlayerHealth;
class QcServerSounds;
class QcServerState;

// The creatures in the server tick (decision E-367): their behaviour and spawning (QcMobManager) with
// what they need to know about the players, the blows they deal to players, their ambient sounds and
// the damage of crammed cells (game rule maxEntityCramming, decision E-401) and what villagers do to
// the world (QcServerVillagers, decision E-411).
class QcServerCreatures
{
public:
    // A creature with an ambient sound makes it on average this often (seconds).
    static constexpr double ambientSeconds = 10.0;
    // Hurt and death sounds of cramming per tick at most (a farm crams hundreds at once).
    static constexpr int crammingSoundsPerTick = 8;

    QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerBlocks &blocks);

    void tick();

private:
    void playAmbientSounds(double dt);
    // Crammed creatures get hurt (QcMobCramming) with their sounds; those that die leave their loot
    // (no experience: no player killed them).
    void applyCramming();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
    QcServerVillagers m_villagers;
};
