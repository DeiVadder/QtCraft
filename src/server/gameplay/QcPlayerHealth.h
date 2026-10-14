#pragma once

#include "QcVec3.h"

#include <QtGlobal>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Health and food of the players (decision E-367): regeneration and starvation, damage (with armour,
// knockback and the short invulnerability after a hit), falls, the void, death with its drops and
// message, respawning, eating, and telling the player.
class QcPlayerHealth
{
public:
    static constexpr double fallDamageThreshold = 3.0;
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

    QcPlayerHealth(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Timers, hunger and the void for every playing player in a simulated tick.
    void tick();
    void damage(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind = DamageKind::Unblockable);
    // Fall damage after a movement step.
    void afterMove(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY);
    void respawn(QcServerClient &client);
    // Eats the held food or puts on the held armour.
    void consume(QcServerClient &client);
    void sendHealth(QcServerClient &client);
    void sendFood(QcServerClient &client);

private:
    void tickPlayer(QcServerClient &client, double dt);
    void applyVoidDamage(QcServerClient &client);
    void die(QcServerClient &client, DamageKind kind);
    // The part of a hit the player's armour lets through; the armour wears down.
    double throughArmour(QcServerClient &client, double amount);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
