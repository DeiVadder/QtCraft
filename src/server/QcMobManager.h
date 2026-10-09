#pragma once

#include "QcChunkPos.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"

#include <QList>
#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QcServerWorld;

// Creatures on the server: spawning by time of day, place and data definitions, a small behaviour
// model (wander, chase, attack, flee when hurt for passive mobs), damage and despawning. Peaceful
// animals (not hostile, ambient or flying) are persistent (decision E-086): a share of newly generated chunks gets a group of them,
// they never despawn, rest while their chunk is not loaded and are saved with the world. Other
// creatures are not saved; they appear again around the players.
class QcMobManager
{
public:
    static constexpr double spawnRadiusMin = 20.0;
    static constexpr double spawnRadiusMax = 48.0;
    static constexpr double despawnDistance = 96.0;
    // Share of newly generated chunks that get a group of animals.
    static constexpr double chunkAnimalChance = 0.1;
    // Animals spawn naturally around a player only while none is awake within this distance.
    static constexpr double animalSearchDistance = 128.0;
    static constexpr int hostileCapPerPlayer = 8;
    static constexpr int passiveCapPerPlayer = 6;
    // Ambient creatures (fireflies) around one player at most.
    static constexpr int ambientCapPerPlayer = 10;
    // Flying creatures keep between these heights above the ground below them.
    static constexpr double flyMinHeight = 1.2;
    static constexpr double flyMaxHeight = 3.0;
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
        // Never despawns and is saved with the world (peaceful animals).
        bool persistent = false;
        // In a chunk that is not loaded: not ticked, not sent, not counted.
        bool dormant = false;
        // Spawned by a rule for the night only: leaves by day like night creatures.
        bool nightRule = false;
    };

    // A persistent mob as saved with the world.
    struct Stored
    {
        int type = 0;
        QcVec3 position;
        float yaw = 0.0f;
        double health = 0.0;
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
    // Damage without a source (lava): no knockback, no fleeing. Returns true when the mob died and
    // was removed.
    bool hurt(quint32 mobId, double amount);
    // Removes a mob without a death (caught); false when there is none.
    bool remove(quint32 mobId);
    // Spawns a mob directly (commands, tests); returns its id.
    quint32 spawn(int type, const QcVec3 &position, quint32 id, bool underground = false, bool persistent = false);
    void removeAll() { m_mobs.clear(); }

    // Gives a chunk seen for the first time its group of animals (chunkAnimalChance, decided by the
    // seed) and remembers it; later calls for the same chunk do nothing. The chunk must be loaded.
    void populateChunk(const QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId);
    bool isPopulated(QcChunkPos pos) const { return m_populated.contains(pos); }
    const QSet<QcChunkPos> &populatedChunks() const { return m_populated; }
    // Persistent mobs to save, and loading them back (as dormant until the next tick sees their chunk).
    QList<Stored> persistentMobs() const;
    void restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId);
    // Mobs that are not dormant.
    qsizetype awakeCount() const;
    // Whether the seed lets a mob with a `spawnChunkChance` below 1 spawn in this chunk (slime chunks);
    // the second form takes the share and index of any spawn rule (0: main rule), each rule has its
    // own set of chunks.
    bool spawnsInChunk(int type, QcChunkPos pos) const;
    bool spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const;

    const QList<Mob> &mobs() const { return m_mobs; }
    const Mob *find(quint32 id) const;

private:
    void trySpawnAround(QcServerWorld &world, const QcVec3 &centre, bool night, const QList<PlayerView> &players,
                        const std::function<quint32()> &nextEntityId);
    void think(Mob &mob, QcServerWorld &world, const QList<PlayerView> &players, double dt, QList<PlayerHit> &hits);
    // Drifting flight without gravity at a few metres above the ground.
    void fly(Mob &mob, const QcMobDefinition &definition, QcServerWorld &world, double dt);

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator m_random;
    QList<Mob> m_mobs;
    QSet<QcChunkPos> m_populated;
    // Spawn rules of every kind (main rule first), indexed by type.
    QList<QList<QcMobDefinition::SpawnRule>> m_rules;
    double m_spawnTimer = 0.0;
};
