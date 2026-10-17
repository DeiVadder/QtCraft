#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcChunkPos.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerLimits.h"
#include "server/mobs/QcMobManager.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <functional>
#include <memory>
#include <optional>

class QcChunk;
class QcServerWorld;

// Creatures that come by themselves (decision E-422): every second up to four spawn attempts around
// each living player by time of day, place, light and the data's spawn rules, within the caps per
// player; and the creatures of a chunk seen for the first time: its group of persistent animals
// (decision E-086) and the creatures of the village pieces in it (decision E-404). Part of
// QcMobManager, which owns the creatures; it draws from the manager's random sequence.
class QcNaturalSpawner
{
public:
    static constexpr double spawnRadiusMin = 20.0;
    static constexpr double spawnRadiusMax = 48.0;
    // Share of newly generated chunks that get a group of animals.
    static constexpr double chunkAnimalChance = 0.1;
    // Animals spawn naturally around a player only while none is awake within this distance.
    static constexpr double animalSearchDistance = 128.0;
    // Creatures spawning by themselves around one player at most by default (QcServerLimits
    // hostileCreaturesPerPlayer, passiveCreaturesPerPlayer, ambientCreaturesPerPlayer: fireflies).
    static constexpr int hostileCapPerPlayer = int(QcServerLimits{}.hostileCreaturesPerPlayer);
    static constexpr int passiveCapPerPlayer = int(QcServerLimits{}.passiveCreaturesPerPlayer);
    static constexpr int ambientCapPerPlayer = int(QcServerLimits{}.ambientCreaturesPerPlayer);
    // Blocks of rock above a cave floor for it to count as dark enough for cave spawns.
    static constexpr int caveDepth = 8;

    using PlayerView = QcMobManager::PlayerView;
    using NextId = std::function<quint32()>;

    // `mobs` owns the creatures this adds and `random` is its sequence; both outlive the spawner.
    QcNaturalSpawner(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed, QRandomGenerator &random);

    // Creatures spawning by themselves around one player at most, by kind.
    void setCaps(int hostile, int passive, int ambient);
    // Advances the spawn timer; once a second, with natural spawning on, tries around every living
    // player.
    void tick(QcServerWorld &world, const QList<PlayerView> &players, bool night, double dt, const NextId &nextEntityId);
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
                        const NextId &nextEntityId);
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
                    const NextId &nextEntityId);
    // One member of a group; false when there is no room for it.
    bool spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    bool spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    // Peaceful kinds that may live on `floor` (chunk-local ground block) of a newly seen chunk.
    QList<int> animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const;
    // Weighted choice by spawn weight for a `random` value in [0, 1).
    int pickAnimal(const QList<int> &candidates, double random) const;

    QcMobManager &m_mobs;
    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QRandomGenerator &m_random;
    int m_hostileCap = hostileCapPerPlayer;
    int m_passiveCap = passiveCapPerPlayer;
    int m_ambientCap = ambientCapPerPlayer;
    // Spawn rules of every kind (main rule first), indexed by type.
    QList<QList<QcMobDefinition::SpawnRule>> m_rules;
    double m_spawnTimer = 0.0;
};
