#pragma once

#include "QcBlockDefinition.h"
#include "QcVec3.h"

#include <QList>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// Health and food of the players, damage, death and respawn, and fights between players and mobs.
class QcServerCombat
{
public:
    // Melee reach of players, from the eyes to the target's centre.
    static constexpr double attackReach = 4.0;
    static constexpr double playerAttackCooldown = 0.4;
    static constexpr double fallDamageThreshold = 3.0;
    // A creature with an ambient sound makes it on average this often (seconds).
    static constexpr double mobAmbientSeconds = 10.0;
    // While mining, the arm swings every this many ticks.
    static constexpr int miningSwingTicks = 6;

    QcServerCombat(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Hunger, regeneration and timers of the players, then the mobs with their attacks.
    void tick();
    // Players and mobs touching a damaging block (lava) get hurt; items in it burn.
    void applyContactDamage();
    void damagePlayer(QcServerClient &client, double amount, const QcVec3 &knockback);
    // Fall damage after a movement step.
    void updateHealth(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY);
    void attack(QcServerClient &client, quint32 entityId);
    void respawn(QcServerClient &client);
    void consume(QcServerClient &client);
    void sendHealth(QcServerClient &client);
    void sendFood(QcServerClient &client);

private:
    void tickPlayer(QcServerClient &client, double dt);
    void playMobSounds(double dt);
    void dropLoot(const QList<QcItemDrop> &drops, const QcVec3 &position);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
