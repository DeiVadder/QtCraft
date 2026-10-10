#pragma once

#include "QcChunkPos.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcPhysics.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QcChunk;
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
        // Entity id of the player it chases this tick; 0 for none (debug display of goals).
        quint32 chasing = 0;
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
    // Game rule mobSpawning: without it no creature spawns by itself, neither around players nor
    // with new chunks (decision E-221); /summon and world setups still add them.
    void setNaturalSpawning(bool on) { m_naturalSpawning = on; }
    bool naturalSpawning() const { return m_naturalSpawning; }

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
    using SpawnRule = QcMobDefinition::SpawnRule;

    // Awake mobs near a spawn centre per category and kind, and whether an animal is awake within
    // animalSearchDistance.
    struct NearbyCounts
    {
        int hostile = 0;
        int passive = 0;
        int ambient = 0;
        QHash<int, int> perType;
        bool animalsNear = false;

        int &of(const QcMobDefinition &definition) { return definition.ambient ? ambient : (definition.hostile ? hostile : passive); }
        int of(const QcMobDefinition &definition) const
        {
            return definition.ambient ? ambient : (definition.hostile ? hostile : passive);
        }
    };

    // A block a mob may spawn on (`floorY`), on the surface or on a cave floor. Biome and light are
    // looked up only when a rule asks, and then once.
    struct SpawnPlace
    {
        int x = 0;
        int z = 0;
        int floorY = 0;
        bool cave = false;
        QcBlockId ground = qcAirBlockId;
        std::optional<QString> biome;
        std::optional<int> light;
    };

    // A kind that may spawn at a place, with the first of its rules that allows it.
    struct SpawnCandidate
    {
        int type = 0;
        const SpawnRule *rule = nullptr;
    };

    void trySpawnAround(QcServerWorld &world, const QcVec3 &centre, bool night, const QList<PlayerView> &players,
                        const std::function<quint32()> &nextEntityId);
    NearbyCounts countNear(const QcVec3 &centre) const;
    // A random place around `centre`; consumes the random numbers of one spawn attempt.
    std::optional<SpawnPlace> randomPlace(const QcServerWorld &world, const QcVec3 &centre);
    // A cave floor with room for a mob, searched downwards from a random height below `surface`;
    // -1 when there is none.
    int randomCaveFloor(const QcChunk &chunk, QcBlockPos column, int surface);
    QList<SpawnCandidate> candidatesAt(const QcServerWorld &world, SpawnPlace &place, bool night, const NearbyCounts &counts) const;
    bool ruleAllows(const QcServerWorld &world, SpawnPlace &place, SpawnCandidate candidate, bool night) const;
    // Weighted random choice by the rules' weights.
    SpawnCandidate pick(const QList<SpawnCandidate> &candidates);
    void spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, NearbyCounts &counts,
                    const std::function<quint32()> &nextEntityId);
    // One member of a group; false when there is no room for it.
    bool spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                    const std::function<quint32()> &nextEntityId);
    bool spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                     const std::function<quint32()> &nextEntityId);
    // Peaceful kinds that may live on `floor` (chunk-local ground block) of a newly seen chunk.
    QList<int> animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const;
    // Weighted choice by spawn weight for a `random` value in [0, 1).
    int pickAnimal(const QList<int> &candidates, double random) const;

    void think(Mob &mob, QcServerWorld &world, const QList<PlayerView> &players, double dt, QList<PlayerHit> &hits);
    // Walking about, standing still or fleeing when hurt.
    QcPhysics::Steering wander(Mob &mob, const QcMobDefinition &definition, double dt);
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
    bool m_naturalSpawning = true;
};
