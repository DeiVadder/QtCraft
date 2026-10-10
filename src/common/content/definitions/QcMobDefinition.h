#pragma once

#include "QcBlockDefinition.h"
#include "QcVec3.h"
#include "QcWorldConstants.h"

#include <QList>
#include <QString>

#include <algorithm>

// One box of a mob's model, in metres relative to the mob's feet centre, facing north (-Z).
struct QcMobPart
{
    QcVec3 from;
    QcVec3 to;
    QString texture;
};

// Data definition of a creature: stats, behaviour, spawning, model and drops.
struct QcMobDefinition
{
    enum class SpawnTime { Any, Day, Night };

    QString name;
    QString translationKey;
    // Translation key of the handbook's text about it ("<translation>.description" unless set); a
    // missing text leaves only the facts. Server side only.
    QString descriptionKey;
    double maxHealth = 10.0;
    double width = 0.6;
    double height = 1.8;
    // Movement speed in m/s while wandering; chasing uses chaseSpeed.
    double walkSpeed = 1.5;
    double chaseSpeed = 3.5;
    bool hostile = false;
    double attackDamage = 2.0;
    double attackRange = 1.6;
    double attackCooldown = 1.0;
    double sightRange = 16.0;
    // Spawning: time of day, blocks the mob may spawn on (empty: any solid), group size and
    // relative weight among the mobs that may spawn at a place.
    SpawnTime spawnTime = SpawnTime::Any;
    QList<QString> spawnBlocks;
    int maxGroup = 2;
    double spawnWeight = 1.0;
    // Hostile mobs that need darkness also spawn underground at any time of day.
    bool spawnsInCaves = false;
    // Brightest block light (torches) at the feet that still allows spawning, 0..15.
    int maxSpawnLight = 15;
    // Share of chunks (chosen by the world seed, separately for each kind) where it spawns at all,
    // like slime chunks; 1 for every chunk. Server side only.
    double spawnChunkChance = 1.0;
    // Highest block its feet may stand in when spawning. Server side only.
    int spawnMaxY = QcWorld::worldHeight;
    // Either static boxes (`parts`) or an animated skeleton model with an outfit.
    QList<QcMobPart> parts;
    QString skeleton;
    QString outfit;
    // Skeleton mobs that walk with their arms stretched forward (zombies).
    bool armsForward = false;
    QList<QcItemDrop> drops;
    // Sounds: now and then while alive, when hit and when killed; empty names are silent.
    QString ambientSound;
    QString hurtSound;
    QString deathSound;
    // Flies (no gravity) and drifts a little above the ground.
    bool flying = false;
    // Drawn at least this bright (0..15): glowing creatures. It lights nothing around them.
    int glow = 0;
    // Ambient creatures (fireflies) have their own spawn cap and never count as passive or hostile.
    bool ambient = false;
    // Most creatures of this kind near one player; 0 for no limit beyond the category's cap.
    int maxCount = 0;
    // Biomes it spawns in; empty for all.
    QStringList spawnBiomes;

    // One place and time where the creature spawns: the main rule (the fields above) or an extra
    // one (`spawn.also`, e.g. slimes also on swamp surfaces at night). Server side only.
    struct SpawnRule
    {
        SpawnTime time = SpawnTime::Any;
        QList<QString> blocks;
        double weight = 1.0;
        bool caves = false;
        int maxLight = 15;
        double chunkChance = 1.0;
        int maxY = QcWorld::worldHeight;
        QStringList biomes;
    };
    QList<SpawnRule> extraSpawns;
    // The main rule followed by the extra ones.
    QList<SpawnRule> spawnRules() const
    {
        QList<SpawnRule> rules{{spawnTime, spawnBlocks, spawnWeight, spawnsInCaves, maxSpawnLight, spawnChunkChance, spawnMaxY, spawnBiomes}};
        rules.append(extraSpawns);
        return rules;
    }
    static constexpr int maxExtraSpawns = 8;
    // Whether any rule lets it spawn by itself; with all weights 0 it only comes from /summon and
    // world setups (the showcase's mannequin).
    bool spawnsNaturally() const
    {
        return spawnWeight > 0.0 || std::ranges::any_of(extraSpawns, [](const SpawnRule &rule) { return rule.weight > 0.0; });
    }
    // Using `catchWith` on the creature turns one of those items into `catchItem` and removes it.
    QString catchWith;
    QString catchItem;

    static constexpr int maxGlow = 15;
};
