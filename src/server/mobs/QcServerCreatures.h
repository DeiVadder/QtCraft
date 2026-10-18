#pragma once

#include "server/mobs/QcCreatureTraits.h"
#include "server/mobs/QcInfestation.h"
#include "server/mobs/QcMobManager.h"
#include "server/villages/QcServerVillagers.h"

class QcPlayerHealth;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// The creatures in the server tick (decision E-367): their behaviour and spawning (QcMobManager) with
// what they need to know about the players, the blows they deal to players, their ambient sounds and
// the damage of crammed cells (game rule maxEntityCramming, decision E-401), what villagers do to
// the world (QcServerVillagers, decision E-411), what creatures with traits do (QcCreatureTraits,
// decision E-460: blocks taken and placed, teleports) and creatures hiding in blocks (QcInfestation,
// decision E-453).
class QcServerCreatures
{
public:
    // A creature with an ambient sound makes it on average this often (seconds).
    static constexpr double ambientSeconds = 10.0;
    // Hurt and death sounds of cramming per tick at most (a farm crams hundreds at once).
    static constexpr int crammingSoundsPerTick = 8;
    // How long what a player fought counts for the player's pets (QcServerClient::Foes), in ticks.
    static constexpr quint64 foeTicks = 100;

    QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerBlocks &blocks);

    void tick();
    // Shows the clients the creatures that teleported since the creatures' tick (projectiles and
    // burning make them teleport too) at their new place in this tick (QcCreatureTraits).
    void announceTeleports();

private:
    // What the creatures know about the players this tick.
    QList<QcMobManager::PlayerView> playerViews() const;
    // A creature's blow at a player, with the effects of its kind's blows.
    void strike(QcServerClient &client, const QcMobManager::PlayerHit &hit);
    void playAmbientSounds(double dt);
    // The blows creatures dealt each other (a golem's, decision E-471; a pet's, decision E-480), with
    // the attacker's sound; those that die leave their loot (no experience).
    void applyCreatureHits();
    // The blows creatures dealt players, a golem's throwing them upwards, with the striker as the
    // player's foe (pets defend, decision E-480).
    void applyPlayerHits(const QList<QcMobManager::PlayerHit> &hits);
    // Crammed creatures get hurt (QcMobCramming) with their sounds; those that die leave their loot
    // (no experience: no player killed them).
    void applyCramming();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
    QcServerVillagers m_villagers;
    QcCreatureTraits m_traits;
    QcInfestation m_infestation;
};
