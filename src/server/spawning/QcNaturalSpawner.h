#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcChunkPos.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerLimits.h"
#include "server/mobs/QcMobManager.h"
#include "server/spawning/QcMonsterCap.h"
#include "server/spawning/QcSpawnLight.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <limits>
#include <memory>
#include <optional>

class QcChunk;
class QcServerWorld;

// Creatures that come by themselves (decision E-422): monsters every tick around the living players
// by the original game's rules (decision E-836: QcSpawnLight, QcSpawnGround, QcMonsterCap), the
// other kinds every second up to four spawn attempts around each living player by time of day,
// place, light and the data's spawn rules, within the caps per player; and the creatures of a chunk
// seen for the first time: its group of persistent animals (decision E-086) and the creatures of the
// village pieces in it (decision E-404). Part of QcMobManager, which owns the creatures; it draws from
// the manager's random sequence, monsters from a sequence of their own.
class QcNaturalSpawner
{
public:
    // Peaceful, ambient and water creatures appear this far from the player they spawn around.
    static constexpr double spawnRadiusMin = 20.0;
    static constexpr double spawnRadiusMax = 48.0;
    // Monsters appear at least this far from every player and at most this far from the nearest
    // (beyond it they would vanish at once, QcMobManager::despawnDistance).
    static constexpr double monsterRadiusMin = 24.0;
    static constexpr double monsterRadiusMax = 128.0;
    // Places tried for monsters around each living player in every tick, in its area of
    // QcMonsterCap::areaChunks chunks.
    static constexpr int monsterAttemptsPerTick = 2;
    // Share of newly generated chunks that get a group of animals.
    static constexpr double chunkAnimalChance = 0.1;
    // Animals spawn naturally around a player only while none is awake within this distance.
    static constexpr double animalSearchDistance = 128.0;
    // Creatures spawning by themselves by default (QcServerLimits monstersPer289Chunks: monsters per
    // QcMonsterCap::areaChunks loaded chunks around players; passiveCreaturesPerPlayer,
    // ambientCreaturesPerPlayer: fireflies, around one player).
    static constexpr int monsterCapPer289Chunks = int(QcServerLimits{}.monstersPer289Chunks);
    static constexpr int passiveCapPerPlayer = int(QcServerLimits{}.passiveCreaturesPerPlayer);
    static constexpr int ambientCapPerPlayer = int(QcServerLimits{}.ambientCreaturesPerPlayer);
    // Blocks of rock above a cave floor for it to count as dark enough for cave spawns.
    static constexpr int caveDepth = 8;

    // Whether creatures of the kind spawn as monsters (QcMonsterCap): hostile ones that walk or fly
    // on land, not water or ambient creatures.
    static bool isMonster(const QcMobDefinition &definition) { return definition.hostile && !definition.aquatic && !definition.ambient; }

    // The time of day and how dark the sky is (QcSpawnLight::skyDarkening).
    struct Sky
    {
        bool night = false;
        int darkening = 0;
    };

    using PlayerView = QcMobManager::PlayerView;
    using NextId = std::function<quint32()>;

    // `mobs` owns the creatures this adds and `random` is its sequence; both outlive the spawner.
    QcNaturalSpawner(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed, QRandomGenerator &random);

    // Monsters per QcMonsterCap::areaChunks loaded chunks around players, and the other kinds around
    // one player at most.
    void setCaps(int hostile, int passive, int ambient);
    // Water creatures spawning by themselves around one player at most (decision E-502).
    void setWaterCap(int water) { m_waterCap = water; }
    // With natural spawning on: monsters around every living player, and once a second the other
    // kinds.
    void tick(QcServerWorld &world, const QList<PlayerView> &players, Sky sky, double dt, const NextId &nextEntityId);
    // The monster cap of this moment: the areas of the living players and the monsters in them.
    QcMonsterCap monsterCap(const QcServerWorld &world, const QList<PlayerView> &players) const;
    // The persistent creatures of the village plan whose spot lies in the chunk.
    void spawnVillageMobs(const QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId);
    // The chunk's group of animals (chunkAnimalChance, decided by the seed) on the highest fitting
    // ground of a random column.
    void spawnChunkAnimals(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, const NextId &nextEntityId);
    // Whether the seed lets a mob with a `spawnChunkChance` below 1 spawn in this chunk (slime chunks);
    // the second form takes the share and index of any spawn rule (0: main rule), each rule has its
    // own set of chunks.
    bool spawnsInChunk(int type, QcChunkPos pos) const;
    bool spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const;

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
        int water = 0;
        QHash<int, int> perType;
        bool animalsNear = false;

        int of(const QcMobDefinition &definition) const
        {
            return definition.aquatic ? water : (definition.ambient ? ambient : (definition.hostile ? hostile : passive));
        }
        // Creatures of kind `type` that may still spawn by its own limit (`maxCount`, 0: none).
        int roomOfKind(const QcMobDefinition &definition, int type) const
        {
            return definition.maxCount > 0 ? definition.maxCount - perType.value(type) : std::numeric_limits<int>::max();
        }
        // Counts one more creature of kind `type`.
        void add(const QcMobDefinition &definition, int type)
        {
            int &count = definition.aquatic ? water : (definition.ambient ? ambient : (definition.hostile ? hostile : passive));
            count += 1;
            perType[type] += 1;
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
        std::optional<int> skyLight;
        std::optional<QcSpawnLight::Draws> draws;

        // The biome and the block and sky light above the floor, looked up once.
        const QString &biomeIn(const QcServerWorld &world);
        int lightIn(const QcServerWorld &world);
        int skyLightIn(const QcServerWorld &world);
        // Whether the cell above the floor passes the monsters' light test (QcSpawnLight), with
        // the draws of the place, drawn once.
        bool darkEnough(const QcServerWorld &world, Sky sky, int maxBlockLight, QRandomGenerator &random);
    };

    // A kind that may spawn at a place, with the first of its rules that allows it.
    struct SpawnCandidate
    {
        int type = 0;
        const SpawnRule *rule = nullptr;
    };

    // Ticks at least between two counts of the loaded chunks while the areas stay (currentMonsterCap).
    static constexpr int areaRecountTicks = 20;
    static QList<QcChunkPos> areaCentres(const QList<PlayerView> &players);
    // The cap with the monsters of the world counted in.
    QcMonsterCap withMonsters(QcMonsterCap cap) const;
    // monsterCap with the loaded chunks counted again only when they or the areas changed (m_areaChunks).
    QcMonsterCap currentMonsterCap(const QcServerWorld &world, const QList<PlayerView> &players);

    // Which kinds a pass considers: monsters (isMonster) or the others.
    enum class Pass : quint8 { Monsters, Others };

    void trySpawnAround(QcServerWorld &world, const QcVec3 &centre, Sky sky, const QList<PlayerView> &players,
                        const NextId &nextEntityId);
    // Up to monsterAttemptsPerTick places around each living player, within the monster cap.
    void spawnMonsters(QcServerWorld &world, const QList<PlayerView> &players, Sky sky, const NextId &nextEntityId);
    // A group of monsters at the place, at most `room`; returns how many spawned.
    int spawnMonsterAt(QcServerWorld &world, SpawnPlace place, Sky sky, int room, const NextId &nextEntityId);
    // Awake creatures within `radius` of `centre`, by category and kind.
    NearbyCounts countNear(const QcVec3 &centre, double radius) const;
    // A random place around `centre`; consumes the random numbers of one spawn attempt.
    std::optional<SpawnPlace> randomPlace(const QcServerWorld &world, const QcVec3 &centre);
    // A random place in the chunks of the monster area around `centre` (QcMonsterCap::reach): one of
    // the floors of a random column.
    std::optional<SpawnPlace> randomMonsterPlace(const QcServerWorld &world, const QcVec3 &centre);
    // The surface of the column or, half of the time, a cave floor well below it.
    std::optional<SpawnPlace> placeInColumn(const QcServerWorld &world, QcBlockPos column);
    // A cave floor with room for a mob, searched downwards from a random height below `surface`;
    // -1 when there is none.
    int randomCaveFloor(const QcChunk &chunk, QcBlockPos column, int surface);
    // The kinds of the pass that may spawn at the place and still have room by `counts`.
    QList<SpawnCandidate> candidatesAt(const QcServerWorld &world, SpawnPlace &place, Sky sky, const NearbyCounts &counts, Pass pass) const;
    bool ruleAllows(const QcServerWorld &world, SpawnPlace &place, SpawnCandidate candidate, Sky sky) const;
    // Weighted random choice by the rules' weights.
    SpawnCandidate pick(const QList<SpawnCandidate> &candidates);
    // A group of the chosen kind, at most `room` members; returns how many spawned.
    int spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, int room, const NextId &nextEntityId);
    // One member of a group; false when there is no room for it.
    bool spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    bool spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    // A water creature in the water below the surface `place` (decision E-502).
    bool spawnSwimmer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    // Peaceful kinds that may live on `floor` (chunk-local ground block) of a newly seen chunk.
    QList<int> animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const;
    // Weighted choice by spawn weight for a `random` value in [0, 1).
    int pickAnimal(const QList<int> &candidates, double random) const;

    QcMobManager &m_mobs;
    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator &m_random;
    // The monsters' own sequence (spawnMonsters), from the seed.
    QRandomGenerator m_monsterRandom;
    // The areas of the last monster cap and their loaded chunks (currentMonsterCap).
    QList<QcChunkPos> m_areaCentres;
    qsizetype m_areaChunks = 0;
    quint64 m_areaEpoch = 0;
    int m_areaTicks = 0;
    // The sequence the running pass draws from: m_random, or m_monsterRandom while monsters spawn.
    QRandomGenerator *m_draws = nullptr;
    int m_hostileCap = monsterCapPer289Chunks;
    int m_passiveCap = passiveCapPerPlayer;
    int m_ambientCap = ambientCapPerPlayer;
    int m_waterCap = int(QcServerLimits{}.waterCreaturesPerPlayer);
    // Spawn rules of every kind (main rule first), indexed by type.
    QList<QList<QcMobDefinition::SpawnRule>> m_rules;
    double m_spawnTimer = 0.0;
};
