#pragma once

#include "QcServerLimits.h"
#include "QcChunkPos.h"
#include "QcContentRegistry.h"
#include "QcCreature.h"
#include "QcMobCrowd.h"
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
class QcMobBehaviour;
class QcPathPlanner;
class QcServerWorld;

// Creatures on the server: spawning by time of day, place and data definitions, their behaviour
// (QcMobBehaviour: wander, chase along paths, attack, flee when hurt for passive mobs), damage and
// despawning. Peaceful
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
    // Creatures spawning by themselves around one player at most by default (QcServerLimits
    // hostileCreaturesPerPlayer, passiveCreaturesPerPlayer, ambientCreaturesPerPlayer: fireflies).
    static constexpr int hostileCapPerPlayer = int(QcServerLimits{}.hostileCreaturesPerPlayer);
    static constexpr int passiveCapPerPlayer = int(QcServerLimits{}.passiveCreaturesPerPlayer);
    static constexpr int ambientCapPerPlayer = int(QcServerLimits{}.ambientCreaturesPerPlayer);
    // Flying creatures keep between these heights above the ground below them.
    static constexpr double flyMinHeight = 1.2;
    static constexpr double flyMaxHeight = 3.0;
    // Blocks of rock above a cave floor for it to count as dark enough for cave spawns.
    static constexpr int caveDepth = 8;
    static constexpr double hurtInvulnerability = 0.5;
    static constexpr double knockbackSpeed = 6.0;

    // A persistent mob as saved with the world.
    struct Stored
    {
        int type = 0;
        QcVec3 position;
        float yaw = 0.0f;
        double health = 0.0;
        double breedCooldown = 0.0;
        double growUp = 0.0;
        bool bred = false;

        bool operator==(const Stored &) const = default;
    };

    // What the mob logic needs to know about a player.
    struct PlayerView
    {
        quint32 entityId = 0;
        QcVec3 position;
        bool alive = true;
        // Creatures leave invulnerable players (creative mode) alone.
        bool targetable = true;
    };

    struct PlayerHit
    {
        quint32 playerEntityId = 0;
        double damage = 0.0;
        QcVec3 knockback;
    };

    QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed);
    ~QcMobManager();
    // Creatures spawning by themselves around one player at most, by kind.
    void setCaps(int hostile, int passive, int ambient);
    // Budgets of the path searches (QcServerLimits pathNodesPerTick, pathNodesPerSearch, pathLength).
    void setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength);
    // The path searches of the creatures (budget use, tests and debug).
    const QcPathPlanner &paths() const;
    // Thresholds of the AI tiers in crowds (QcServerLimits crowdThrottled, crowdIdle; decision E-401).
    void setCrowdLimits(qint64 throttled, qint64 idle);
    // What the creatures did in the last tick: crowds, tiers, paths (tests, measurements).
    const QcMobBehaviour &behaviour() const { return *m_behaviour; }
    // The creatures that stood in a cell with more than `limit` creatures at the start of the last
    // tick (game rule maxEntityCramming; 0: none).
    QList<QcMobCrowd::Crammed> crammed(int limit) const;

    // Advances all mobs by one tick and returns the hits they dealt to players.
    QList<PlayerHit> tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime, double dt,
                          const std::function<quint32()> &nextEntityId);
    // Applies damage from a player at `from`; returns the mob's drops when it died.
    std::optional<QList<QcItemDrop>> damage(quint32 mobId, double amount, const QcVec3 &from);
    // Damage without a source (lava): no knockback, no fleeing. Returns true when the mob died and
    // was removed.
    bool hurt(quint32 mobId, double amount);
    // The same, returning the mob's drops when it died (cramming, decision E-401).
    std::optional<QList<QcItemDrop>> wound(quint32 mobId, double amount);
    // The same for the mob at `index` in mobs(); a death removes it from the list.
    std::optional<QList<QcItemDrop>> woundAt(qsizetype index, double amount);
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
    // The chunk left the world; whether it got its animals goes with it (decision E-374).
    void forgetPopulated(QcChunkPos pos) { m_populated.remove(pos); }
    void notePopulated(QcChunkPos pos) { m_populated.insert(pos); }
    // Persistent mobs to save, and loading them back (as dormant until the next tick sees their chunk).
    QList<Stored> persistentMobs() const;
    static Stored storedOf(const QcCreature &mob);
    void restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId);
    // Mobs that are not dormant, all of them and those that came by /summon.
    qsizetype awakeCount() const;
    qsizetype awakeSummonedCount() const;
    // Whether the seed lets a mob with a `spawnChunkChance` below 1 spawn in this chunk (slime chunks);
    // the second form takes the share and index of any spawn rule (0: main rule), each rule has its
    // own set of chunks.
    bool spawnsInChunk(int type, QcChunkPos pos) const;
    bool spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const;

    const QList<QcCreature> &mobs() const { return m_mobs; }
    // For the breeding timers (QcMobBreeding); never add or remove mobs through it.
    QList<QcCreature> &editableMobs() { return m_mobs; }
    const QcCreature *find(quint32 id) const;
    QcCreature *find(quint32 id);

private:
    int capOf(const QcMobDefinition &definition) const;
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

    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator m_random;
    // Uses m_random, so it comes after it.
    std::unique_ptr<QcMobBehaviour> m_behaviour;
    QList<QcCreature> m_mobs;
    int m_hostileCap = hostileCapPerPlayer;
    int m_passiveCap = passiveCapPerPlayer;
    int m_ambientCap = ambientCapPerPlayer;
    QSet<QcChunkPos> m_populated;
    // Spawn rules of every kind (main rule first), indexed by type.
    QList<QList<QcMobDefinition::SpawnRule>> m_rules;
    double m_spawnTimer = 0.0;
    bool m_naturalSpawning = true;
};
