// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/*! \brief Health and food of the players (decision E-367): regeneration and starvation, damage
    (with armour, knockback and the short invulnerability after a hit), falls, the void, death
    with its drops and message, respawning, eating, and telling the player. */
class QcPlayerHealth
{
public:
    /*! A fall higher than this many blocks hurts, one point per block beyond. */
    static constexpr double fallDamageThreshold = 3.0;
    /*! Below QcWorld::voidLimitY a player takes this much damage at once and again every
        interval (decision E-288). \qcshared */
    static constexpr double voidDamage = 4.0;
    static constexpr int voidDamageIntervalTicks = 10;

    /*! \brief Whether armour takes part of the damage and wears down (decision E-234): blows of
        creatures and contact with damaging blocks are armoured; falls are not.

        Command damage (/kill) also reaches invulnerable players (creative mode, decision E-282);
        so does the void, which in addition ignores the short invulnerability after a hit and has
        its own death message (decision E-288). Damage of a status effect is unblockable with a
        death message of its own (decision E-450). A creature's blow and a creature's shot are
        armoured; a player they kill hears who it was (decision E-873, the creature passed with
        the damage). Drowning is unblockable with a death message of its own (decision E-955). */
    enum class DamageKind : quint8 {
        /*! Armour takes nothing (falls). */
        Unblockable,
        /*! Armour takes its part (contact with damaging blocks). */
        Armoured,
        /*! A command (/kill); also reaches invulnerable players. */
        Command,
        /*! The void; also reaches invulnerable players, with its own death message. */
        Void,
        /*! A status effect; unblockable, with its own death message. */
        Effect,
        /*! A creature's blow; armoured. */
        Blow,
        /*! A creature's shot; armoured. */
        Shot,
        /*! Drowning; unblockable, with its own death message. */
        Drowning
    };

    QcPlayerHealth(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Timers, hunger and the void for every playing player in a simulated tick. */
    void tick();
    /*! Hurts the player \a client by \a amount of \a kind, pushed by \a knockback, and returns how
        the hit took: during the invulnerability after a hurt (game rule playerInvulnerabilityTicks)
        only a stronger hit takes, with the difference, silent and without the push (QcHurtWindow,
        decision E-429). \a creature is the entity id of the creature that deals a Blow or a Shot
        (0: none, a dispenser's or a player's arrow); a death by this very hit names it (decision
        E-873). */
    QcHurtWindow::Take damage(QcServerClient &client, double amount, const QcVec3 &knockback, DamageKind kind = DamageKind::Unblockable,
                              quint32 creature = 0);
    /*! Pushes the player \a client along \a direction with \a strength times a blow's push (a
        projectile's knockback, decision E-427, also without damage). */
    static void knock(QcServerClient &client, const QcVec3 &direction, double strength = 1.0);
    /*! Applies the fall damage of \a client after a movement step that started at height
        \a previousY, \a wasOnGround and \a wasInWater telling where it stood before;
        \a maxFallSpeed is the fall limit of the effects the step ran with (0: none). */
    void afterMove(QcServerClient &client, bool wasOnGround, bool wasInWater, double previousY, float maxFallSpeed);
    /*! Returns whether a fall limit of \a maxFallSpeed m/s (0: none) holds a fall below the speed
        it reaches at fallDamageThreshold blocks, so that it never hurts (decision E-992). */
    [[nodiscard]] static bool fallHeldBack(float maxFallSpeed);
    /*! Brings the dead player \a client back at the spawn. */
    void respawn(QcServerClient &client);
    /*! Eats the food \a client holds or puts on the held armour. */
    void consume(QcServerClient &client);
    /*! Drinks the drink \a held by \a client (milk clears the status effects, decision E-500)
        and leaves its container. */
    void drink(QcServerClient &client, const QcItemDefinition &held);
    /*! Sends \a client its health; with \a hurt as a fresh hit from the world yaw \a hurtFrom
        (none: without a direction), so the client tilts the view (decision E-971). */
    void sendHealth(QcServerClient &client, bool hurt = false, std::optional<float> hurtFrom = {});
    /*! Sends \a client its food points. */
    void sendFood(QcServerClient &client);

private:
    void tickPlayer(QcServerClient &client, double dt);
    void applyVoidDamage(QcServerClient &client);
    void die(QcServerClient &client, DamageKind kind, quint32 creature);
    // The chat line that tells the player of the death: with the creature that dealt the killing
    // blow or shot (its custom name, else its kind as a content text, decision E-873).
    QcMsg::ChatMessage deathLine(const QcServerClient &client, DamageKind kind, quint32 creature) const;
    // Projectiles in flight that would teleport the player (shade pearls, decision E-460) forget
    // their thrower: they bring a dead player nowhere, also not after the respawn.
    void forgetTeleports(const QcServerClient &client);
    // The part of a hit the player's armour lets through; the armour wears down.
    double throughArmour(QcServerClient &client, double amount);
    // The part of a hit that resistance and the extra health of absorption let through (decision
    // E-992); absorption wears down.
    double throughEffects(QcServerClient &client, double amount) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
