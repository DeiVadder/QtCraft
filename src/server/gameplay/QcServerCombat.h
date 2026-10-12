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
    // Below QcWorld::voidLimitY a player takes this much damage at once and again every interval
    // (decision E-288).
    static constexpr double voidDamage = 4.0;
    static constexpr int voidDamageIntervalTicks = 10;

    // Whether armour takes part of the damage and wears down (decision E-234): blows of creatures
    // and contact with damaging blocks are armoured; falls are not. Command damage (/kill) also
    // reaches invulnerable players (creative mode, decision E-282); so does the void, which in
    // addition ignores the short invulnerability after a hit and has its own death message
    // (decision E-288).
    enum class DamageKind : quint8 { Unblockable, Armoured, Command, Void };

    QcServerCombat(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Hunger, regeneration and timers of the players, then the mobs with their attacks.
    void tick();
    // Players and mobs touching a damaging block (lava) get hurt; items in it burn.
    void applyContactDamage();
    void damagePlayer(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind = DamageKind::Unblockable);
    // Fall damage after a movement step.
    void updateHealth(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY);
    void attack(QcServerClient &client, quint32 entityId);
    void respawn(QcServerClient &client);
    void consume(QcServerClient &client);
    void sendHealth(QcServerClient &client);
    void sendFood(QcServerClient &client);

private:
    void tickPlayer(QcServerClient &client, double dt);
    void applyVoidDamage(QcServerClient &client);
    void die(QcServerClient &client, DamageKind kind);
    // The part of a hit the player's armour lets through; the armour wears down.
    double throughArmour(QcServerClient &client, double amount);
    void playMobSounds(double dt);
    void dropLoot(const QList<QcItemDrop> &drops, const QcVec3 &position);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
