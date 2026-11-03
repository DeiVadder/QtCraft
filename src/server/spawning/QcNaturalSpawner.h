// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcSpawnWeights.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcChunkPos.h"
#include "common/core/QcCreatureKindId.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerLimits.h"
#include "server/mobs/QcMobManager.h"
#include "server/spawning/QcChunkSpawns.h"
#include "server/spawning/QcMonsterCap.h"
#include "server/spawning/QcSpawnAreas.h"
#include "server/spawning/QcSpawnLight.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <optional>

class QcChunk;
class QcServerWorld;

/*! \brief Creatures that come by themselves (decision E-422): monsters every tick around the living
    players by the original game's rules (decision E-836: QcSpawnLight, QcSpawnGround, QcMonsterCap;
    in the spawn areas of structures only their kinds, decision E-871: QcSpawnAreas), the other
    kinds every second up to four spawn attempts around each living player by time of day, place,
    light and the data's spawn rules, within the caps per player; and the creatures of a chunk seen
    for the first time: its group of persistent animals (decision E-086) and the creatures of the
    village pieces in it (decision E-404).

    Part of QcMobManager, which owns the creatures; it draws from the manager's random sequence,
    monsters from a sequence of their own. */
class QcNaturalSpawner
{
public:
    /*! Peaceful, ambient and water creatures appear this far from the player they spawn around.
        \qcshared */
    static constexpr double spawnRadiusMin = 20.0;
    static constexpr double spawnRadiusMax = 48.0;
    /*! Monsters appear at least this far from every player and at most this far from the nearest
        (beyond it they would vanish at once, QcMobManager::despawnDistance). \qcshared */
    static constexpr double monsterRadiusMin = 24.0;
    static constexpr double monsterRadiusMax = 128.0;
    /*! Places tried for monsters around each living player in every tick, in its area of
        QcMonsterCap::areaChunks chunks. */
    static constexpr int monsterAttemptsPerTick = 2;
    /*! Share of newly generated chunks that get a group of animals. */
    static constexpr double chunkAnimalChance = QcChunkSpawns::chunkAnimalChance;
    /*! Animals spawn naturally around a player only while none is awake within this distance. */
    static constexpr double animalSearchDistance = 128.0;
    /*! Creatures spawning by themselves by default (QcServerLimits monstersPer289Chunks:
        monsters per QcMonsterCap::areaChunks loaded chunks around players;
        passiveCreaturesPerPlayer, ambientCreaturesPerPlayer: fireflies, around one player).
        \qcshared */
    static constexpr int monsterCapPer289Chunks = static_cast<int>(QcServerLimits{}.monstersPer289Chunks);
    static constexpr int passiveCapPerPlayer = static_cast<int>(QcServerLimits{}.passiveCreaturesPerPlayer);
    static constexpr int ambientCapPerPlayer = static_cast<int>(QcServerLimits{}.ambientCreaturesPerPlayer);
    /*! Blocks of rock above a cave floor for it to count as dark enough for cave spawns. */
    static constexpr int caveDepth = 8;

    /*! Returns whether creatures of \a definition spawn as monsters (QcMonsterCap): hostile ones
        that walk or fly on land, not water or ambient creatures. */
    static bool isMonster(const QcMobDefinition &definition) { return definition.isMonster(); }

    /*! \brief The time of day and how dark the sky is (QcSpawnLight::skyDarkening). */
    struct Sky
    {
        /*! Whether it is night. */
        bool night = false;
        /*! How dark the sky is. */
        int darkening = 0;
    };

    /*! What the spawner knows about a player (QcMobExchange::PlayerView). */
    using PlayerView = QcMobManager::PlayerView;
    /*! Gives the entity id of the next creature. */
    using NextId = std::function<quint32()>;

    /*! \a mobs owns the creatures this adds and \a random is its sequence; both outlive the
        spawner. Kinds from \a content, chunk chances decided by the world \a seed. */
    QcNaturalSpawner(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed, QRandomGenerator &random);

    /*! Sets the caps: \a hostile monsters per QcMonsterCap::areaChunks loaded chunks around
        players, \a passive and \a ambient creatures around one player at most. */
    void setCaps(int hostile, int passive, int ambient);
    /*! Sets the caps of water creatures spawning by themselves around one player: \a fish for
        fish (decision E-502), \a large for larger ones like the squid
        (QcAquaticDefinition::Cap::Large, decision E-974). */
    void setWaterCaps(int fish, int large)
    {
        m_waterCap = fish;
        m_largeWaterCap = large;
    }
    /*! One tick of \a dt seconds in \a world under \a sky with natural spawning on: monsters
        around every living one of \a players, and once a second the other kinds, with ids from
        \a nextEntityId. */
    void tick(QcServerWorld &world, const QList<PlayerView> &players, Sky sky, double dt, const NextId &nextEntityId);
    /*! Returns the monster cap of this moment in \a world: the areas of the living ones of
        \a players and the monsters in them. */
    QcMonsterCap monsterCap(const QcServerWorld &world, const QList<PlayerView> &players) const;
    /*! Spawns the village creatures of the chunk at \a pos of \a world seen for the first time,
        with ids from \a nextEntityId (QcChunkSpawns). */
    void spawnVillageMobs(const QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId)
    {
        m_chunks.spawnVillageMobs(world, pos, nextEntityId);
    }
    /*! Spawns the group of animals of the new chunk \a chunk at \a pos in \a world, with ids from
        \a nextEntityId (QcNaturalSpawner::chunkAnimalChance). */
    void spawnChunkAnimals(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, const NextId &nextEntityId)
    {
        m_chunks.spawnChunkAnimals(world, pos, chunk, nextEntityId);
    }
    /*! Returns whether the seed lets a mob of the kind \a type with a \c {spawnChunkChance}
        below 1 spawn in the chunk at \a pos (slime chunks). */
    bool spawnsInChunk(QcCreatureKindId type, QcChunkPos pos) const;
    /*! Returns the spawn areas of structures around the players, as of the last monster pass. */
    const QcSpawnAreas &spawnAreas() const { return m_areas; }

private:
    int capOf(const QcMobDefinition &definition) const;
    using SpawnRule = QcMobDefinition::SpawnRule;
    // The caps creatures count against: hostile, passive, ambient, fish and large water creatures.
    static constexpr std::size_t categories = 5;
    // The cap of `definition` as an index below `categories`.
    static std::size_t categoryOf(const QcMobDefinition &definition);

    // Awake mobs near a spawn centre per category and kind, and whether an animal is awake within
    // animalSearchDistance.
    struct NearbyCounts
    {
        std::array<int, categories> perCategory{};
        QHash<QcCreatureKindId, int> perType;
        bool animalsNear = false;

        int of(const QcMobDefinition &definition) const { return perCategory[categoryOf(definition)]; }
        // Creatures of kind `type` that may still spawn by its own limit (`maxCount`, 0: none).
        int roomOfKind(const QcMobDefinition &definition, QcCreatureKindId type) const
        {
            return definition.maxCount > 0 ? definition.maxCount - perType.value(type) : std::numeric_limits<int>::max();
        }
        // Counts one more creature of kind `type`.
        void add(const QcMobDefinition &definition, QcCreatureKindId type)
        {
            perCategory[categoryOf(definition)] += 1;
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
        // The feet are in water above the floor (only monster places, for drowned; decision E-838).
        bool water = false;

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
        QcCreatureKindId type;
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
    // One monster of the area's kinds at the place, drawn alike, by its area rule; how many spawned.
    int spawnInArea(QcServerWorld &world, SpawnPlace place, Sky sky, const QcSpawnAreas::Area &area, const NextId &nextEntityId);
    // Awake creatures within `radius` of `centre`, by category and kind.
    NearbyCounts countNear(const QcVec3 &centre, double radius) const;
    // A random place around `centre`; consumes the random numbers of one spawn attempt.
    std::optional<SpawnPlace> randomPlace(const QcServerWorld &world, const QcVec3 &centre);
    // The places of one attempt in the chunks of the monster area around `centre` (QcMonsterCap::reach):
    // one of the floors of a random column, or every floor when the column lies in a spawn area
    // (decision E-871); none when the column has no floor or is not loaded.
    QList<SpawnPlace> randomMonsterPlaces(const QcServerWorld &world, const QcVec3 &centre);
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
    // One kind of the monster list of `biome` by weight (QcSpawnWeights); std::nullopt when the draw
    // falls on a kind that is not among the candidates.
    std::optional<SpawnCandidate> pickOfBiome(const QList<SpawnCandidate> &candidates, const QString &biome);
    // A group of the chosen kind, at most `room` members; returns how many spawned.
    int spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, int room, const NextId &nextEntityId);
    // One member of a group; false when there is no room for it.
    bool spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    bool spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    // A walker spawning in water (drowned, decision E-838): its feet in the water above the floor.
    bool spawnWader(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);
    // A water creature in the water below the surface `place` (decision E-502).
    bool spawnSwimmer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen, const NextId &nextEntityId);

    QcMobManager &m_mobs;
    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator &m_random;
    // The monsters' own sequence (spawnMonsters), from the seed.
    QRandomGenerator m_monsterRandom;
    QcChunkSpawns m_chunks;
    // The monster lists of the biomes (decision E-838).
    QcSpawnWeights m_weights;
    // The spawn areas of structures (decision E-871) and the rules their kinds spawn by there
    // (QcSpawnAreas::ruleOf), indexed by type.
    QcSpawnAreas m_areas;
    QList<QcMobDefinition::SpawnRule> m_areaRules;
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
    int m_waterCap = static_cast<int>(QcServerLimits{}.waterCreaturesPerPlayer);
    int m_largeWaterCap = static_cast<int>(QcServerLimits{}.largeWaterCreaturesPerPlayer);
    // Spawn rules of every kind (main rule first), indexed by type.
    QList<QList<QcMobDefinition::SpawnRule>> m_rules;
    double m_spawnTimer = 0.0;
};
