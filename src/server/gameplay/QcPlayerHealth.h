#pragma once

#include "common/core/QcVec3.h"
#include "common/protocol/QcSessionMessages.h"
#include "server/entities/QcHurtWindow.h"

#include <QtGlobal>

class QcServerInventory;
struct QcItemDefinition;
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
    // (decision E-288). Damage of a status effect is unblockable with a death message of its own
    // (decision E-450). A creature's blow and a creature's shot are armoured; a player they kill
    // hears who it was (decision E-873, the creature passed with the damage).
    enum class DamageKind : quint8 { Unblockable, Armoured, Command, Void, Effect, Blow, Shot };

    QcPlayerHealth(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Timers, hunger and the void for every playing player in a simulated tick.
    void tick();
    // Hurts the player and returns how the hit took: during the invulnerability after a hurt (game
    // rule playerInvulnerabilityTicks) only a stronger hit takes, with the difference, silent and
    // without the push (QcHurtWindow, decision E-429). `creature` is the entity id of the creature
    // that deals a Blow or a Shot (0: none, a dispenser's or a player's arrow); a death by this very
    // hit names it (decision E-873).
    QcHurtWindow::Take damage(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind = DamageKind::Unblockable,
                              quint32 creature = 0);
    // Pushes the player along `direction` with `strength` times a blow's push (a projectile's
    // knockback, decision E-427, also without damage).
    static void knock(QcServerClient &client, const QcVec3 &direction, double strength = 1.0);
    // Fall damage after a movement step.
    void afterMove(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY);
    void respawn(QcServerClient &client);
    // Eats the held food or puts on the held armour.
    void consume(QcServerClient &client);
    // Drinks the held drink (milk clears the status effects, decision E-500) and leaves its container.
    void drink(QcServerClient &client, const QcItemDefinition &held);
    void sendHealth(QcServerClient &client);
    void sendFood(QcServerClient &client);

private:
    void tickPlayer(QcServerClient &client, double dt);
    void applyVoidDamage(QcServerClient &client);
    void die(QcServerClient &client, DamageKind kind, quint32 creature);
    // The chat line that tells the player of the death: with the creature that dealt the killing
    // blow or shot (its custom name, else its kind as a content text, decision E-873).
    QcMsg::ChatMessage deathLine(const QcServerClient &client, DamageKind kind, quint32 creature) const;
    // Projectiles in flight that would teleport the player (ender pearls, decision E-460) forget
    // their thrower: they bring a dead player nowhere, also not after the respawn.
    void forgetTeleports(const QcServerClient &client);
    // The part of a hit the player's armour lets through; the armour wears down.
    double throughArmour(QcServerClient &client, double amount);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
