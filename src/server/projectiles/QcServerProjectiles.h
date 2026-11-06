// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/explosions/QcServerExplosions.h"
#include "server/projectiles/QcHitEffects.h"
#include "server/projectiles/QcPlayerFishing.h"
#include "server/projectiles/QcPlayerLaunchers.h"
#include "server/projectiles/QcPlayerThrows.h"
#include "server/projectiles/QcProjectileManager.h"

class QcPlayerHealth;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

/*! \brief Projectiles in the server tick (decision E-425): the shots of the creatures' brains of
    this tick (QcMobManager::takeLaunches) become projectiles, then every projectile flies against
    the blocks and the bodies of creatures and players.

    A hit hurts like a blow (armour, the short invulnerability, knockback along the flight, loot and
    experience when a player shot); stuck projectiles go to the players that touch them. Players
    shoot through QcPlayerLaunchers and throw through QcPlayerThrows; what a hit does comes from the
    kind's onHit (QcHitEffects, E-427). Creatures that dodge projectiles teleport away unhurt, and a
    projectile that teleports its thrower (a shade pearl) brings them where it breaks (decision
    E-460); a splash (a splash potion, decision E-624) reaches every living body around the place it
    breaks. Fishing rods cast and reel in their bobbers through QcPlayerFishing (decision E-503); a
    projectile without hit effects (a bobber) bounces off bodies unhurt. */
class QcServerProjectiles
{
public:
    QcServerProjectiles(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerInventory &inventory,
                        QcServerExplosions &explosions);

    /*! Ends draws whose launcher left the hand, then moves every projectile. */
    void tick();
    /*! Returns the players' launchers (bows, crossbows). */
    QcPlayerLaunchers &launchers() { return m_launchers; }
    /*! Returns the players' throws. */
    QcPlayerThrows &throws() { return m_throws; }
    /*! Returns the players' fishing. */
    QcPlayerFishing &fishing() { return m_fishing; }
    /*! The player of \a client used the use button with an item of QcMsg::ThrowItem in the
        hand: a fishing rod fishes, anything else is thrown (QcPlayerThrows). */
    void useHeld(QcServerClient &client);
    /*! Adds \a shot with a new id; returns false beyond the limit or for an unknown kind. */
    bool launch(const QcProjectileManager::Launch &shot);
    /*! Sets \a blockHit, what a projectile meeting a block does to it (a target block gives a
        signal; the server wires QcServerCircuits::hitTarget). */
    void setBlockHit(std::function<void(const QcProjectileManager::BlockHit &)> blockHit) { m_blockHit = std::move(blockHit); }

    /*! \a client strikes the entity \a id: a flying projectile that can be deflected flies on
        where he looks, as his shot (decision E-1053). Returns \c false when \a id is no
        projectile. */
    bool deflect(QcServerClient &client, quint32 id);

    /*! How far from the eyes (metres) a strike reaches a projectile. */
    static constexpr double deflectReach = 6.0;

private:
    QList<QcProjectileFlight::Body> bodies() const;
    QList<QcProjectileManager::PlayerView> collectors() const;
    // Deals the effects of the projectile's onHit (decision E-427) to the body hit.
    bool hit(const QcProjectileManager::Hit &hit);
    // `creature` shot it (0: no creature, decision E-873).
    void hitPlayer(QcServerClient &client, const QcHitEffects::Impact &impact, const QcVec3 &along, quint32 creature);

    /*!
        Applies \a impact of \a hit to the creature it struck, pushed along \a along. Returns
        whether the creature took it; a pet of another player and one that dodges do not.
    */
    bool hitCreature(const QcProjectileManager::Hit &hit, const QcHitEffects::Impact &impact, const QcVec3 &along);
    // The foes of the players' pets (decision E-480): a player's shot at a creature names it as the
    // player's target (true when a player shot); a creature's shot at a player names the shooter,
    // which is returned (0 when no creature shot).
    bool noteShot(quint32 shooter, quint32 creature);
    quint32 noteShot(quint32 shooter, QcServerClient &player);
    bool pickUp(quint32 player, const QcProjectile &projectile);
    // A projectile with a teleport effect broke (a shade pearl, decision E-460): its thrower comes
    // to the nearest safe place there and takes the effect's fall damage.
    void teleportThrower(const QcProjectile &projectile, const QcHitEffect &effect);
    // The creatures of the projectile's spawn effects where it broke (decision E-450).
    void spawnWhereBroken(const QcProjectile &projectile, const QcProjectileDefinition &definition);
    // A splash where it broke (decision E-624): every awake creature and every player in the game
    // within `radius` but its thrower takes the splash's share (QcHitEffects::splashImpact).
    void splashCreatures(const QcProjectile &projectile, const QcProjectileDefinition &definition, double radius);
    void splashPlayers(const QcProjectile &projectile, const QcProjectileDefinition &definition, double radius);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
    QcServerInventory &m_inventory;
    QcServerExplosions &m_explosions;
    QcPlayerLaunchers m_launchers;
    QcPlayerThrows m_throws;
    QcPlayerFishing m_fishing;
    QcHitEffects m_effects;
    QList<QcServerExplosions::Blast> m_blasts;
    std::function<void(const QcProjectileManager::BlockHit &)> m_blockHit;
};
