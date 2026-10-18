#pragma once

#include "server/projectiles/QcHitEffects.h"
#include "server/projectiles/QcPlayerLaunchers.h"
#include "server/projectiles/QcPlayerThrows.h"
#include "server/projectiles/QcProjectileManager.h"

class QcPlayerHealth;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

// Projectiles in the server tick (decision E-425): the shots of the creatures' brains of this tick
// (QcMobManager::takeLaunches) become projectiles, then every projectile flies against the blocks
// and the bodies of creatures and players. A hit hurts like a blow (armour, the short
// invulnerability, knockback along the flight, loot and experience when a player shot); stuck
// projectiles go to the players that touch them. Players shoot through QcPlayerLaunchers and
// throw through QcPlayerThrows; what a hit does comes from the kind's onHit (QcHitEffects, E-427).
// Creatures that dodge projectiles teleport away unhurt, and a projectile that teleports its thrower
// (an ender pearl) brings them where it breaks (decision E-460).
class QcServerProjectiles
{
public:
    QcServerProjectiles(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerInventory &inventory);

    // Ends draws whose launcher left the hand, then moves every projectile.
    void tick();
    QcPlayerLaunchers &launchers() { return m_launchers; }
    QcPlayerThrows &throws() { return m_throws; }
    // Adds a shot with a new id; false beyond the limit or for an unknown kind.
    bool launch(const QcProjectileManager::Launch &shot);

private:
    QList<QcProjectileFlight::Body> bodies() const;
    QList<QcProjectileManager::PlayerView> collectors() const;
    // Deals the effects of the projectile's onHit (decision E-427) to the body hit.
    bool hit(const QcProjectileManager::Hit &hit);
    void hitPlayer(QcServerClient &client, const QcHitEffects::Impact &impact, const QcVec3 &along);
    bool pickUp(quint32 player, const QcProjectile &projectile);
    // A projectile with a teleport effect broke (an ender pearl, decision E-460): its thrower comes
    // to the nearest safe place there and takes the effect's fall damage.
    void teleportThrower(const QcProjectile &projectile, const QcHitEffect &effect);
    // The creatures of the projectile's spawn effects where it broke (decision E-450).
    void spawnWhereBroken(const QcProjectile &projectile, const QcProjectileDefinition &definition);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
    QcServerInventory &m_inventory;
    QcPlayerLaunchers m_launchers;
    QcPlayerThrows m_throws;
    QcHitEffects m_effects;
};
