#pragma once

#include "QcContentRegistry.h"
#include "QcEntityMotion.h"

#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <memory>
#include <optional>

class QcServerWorld;

// Creatures on the server: spawning by time of day, place and data definitions, a small behaviour
// model (wander, chase, attack, flee when hurt for passive mobs), damage and despawning. Mobs are
// not saved; they appear again around the players.
class QcMobManager
{
public:
    static constexpr double spawnRadiusMin = 20.0;
    static constexpr double spawnRadiusMax = 48.0;
    static constexpr double despawnDistance = 96.0;
    static constexpr int hostileCapPerPlayer = 8;
    static constexpr int passiveCapPerPlayer = 6;
    // /summon refuses to raise the total number of mobs above this.
    static constexpr int summonLimit = 64;
    // Blocks of rock above a cave floor for it to count as dark enough for cave spawns.
    static constexpr int caveDepth = 8;
    static constexpr double hurtInvulnerability = 0.5;
    static constexpr double knockbackSpeed = 6.0;

    struct Mob
    {
        quint32 id = 0;
        int type = 0;
        QcEntityMotion motion;
        float yaw = 0.0f;
        double health = 0.0;
        double attackCooldown = 0.0;
        double hurtTimer = 0.0;
        double wanderTimer = 0.0;
        QcVec3 wanderDirection;
        double fleeTimer = 0.0;
        bool hurtThisTick = false;
        // Struck at a player this tick (animation).
        bool attackedThisTick = false;
        bool blockedLastTick = false;
        // Spawned on a cave floor: night creatures there stay in daylight.
        bool underground = false;
    };

    // What the mob logic needs to know about a player.
    struct PlayerView
    {
        quint32 entityId = 0;
        QcVec3 position;
        bool alive = true;
    };

    struct PlayerHit
    {
        quint32 playerEntityId = 0;
        double damage = 0.0;
        QcVec3 knockback;
    };

    QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    // Advances all mobs by one tick and returns the hits they dealt to players.
    QList<PlayerHit> tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime, double dt,
                          const std::function<quint32()> &nextEntityId);
    // Applies damage from a player at `from`; returns the mob's drops when it died.
    std::optional<QList<QcItemDrop>> damage(quint32 mobId, double amount, const QcVec3 &from);
    // Spawns a mob directly (commands, tests); returns its id.
    quint32 spawn(int type, const QcVec3 &position, quint32 id, bool underground = false);
    void removeAll() { m_mobs.clear(); }

    const QList<Mob> &mobs() const { return m_mobs; }
    const Mob *find(quint32 id) const;

private:
    void trySpawnAround(QcServerWorld &world, const QcVec3 &centre, bool night, const QList<PlayerView> &players,
                        const std::function<quint32()> &nextEntityId);
    void think(Mob &mob, QcServerWorld &world, const QList<PlayerView> &players, double dt, QList<PlayerHit> &hits);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator m_random;
    QList<Mob> m_mobs;
    double m_spawnTimer = 0.0;
};
