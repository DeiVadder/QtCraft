#pragma once

#include "QcBlockDefinition.h"
#include "QcVec3.h"

#include <QList>
#include <QString>

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
    // Using `catchWith` on the creature turns one of those items into `catchItem` and removes it.
    QString catchWith;
    QString catchItem;

    static constexpr int maxGlow = 15;
};
