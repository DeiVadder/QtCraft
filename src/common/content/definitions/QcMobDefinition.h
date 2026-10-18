#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/definitions/QcMobTraits.h"
#include "common/core/QcVec3.h"
#include "common/core/QcWorldConstants.h"

#include <QList>
#include <QString>

#include <algorithm>
#include <optional>

// One box of a mob's model, in metres relative to the mob's feet centre, facing north (-Z).
struct QcMobPart
{
    QcVec3 from;
    QcVec3 to;
    QString texture;
};

// How a creature finds its way (decision E-423), from its data (`abilities`); the path rules
// (QcPathRules::of) follow from it. Server side only.
struct QcMobAbilities
{
    enum class Climbing : quint8 {
        None,
        // Holds on to ladders and goes up and down along them.
        Ladders,
        // Also up walls (spiders, stage 52); on paths like Ladders until then.
        Walls,
    };

    // Opens closed wooden doors and gates on its way (only the villager AI does so far).
    bool opensDoors = false;
    Climbing climbs = Climbing::None;
    // Moves through water on its paths as freely as on land; a walker uses water only without a dry way.
    bool swims = false;
    // Drops deeper than the usual limit into deep water (QcPathRules::waterDrop).
    bool divesIntoWater = true;

    bool operator==(const QcMobAbilities &) const = default;
};

// How a creature with the archer AI shoots (decision E-426), from its data (`archer`). Server side
// only.
struct QcArcherDefinition
{
    // Name of the QcProjectileDefinition it shoots.
    QString projectile;
    // Seconds it draws before a shot, and waits after one before it draws again.
    double drawSeconds = 1.0;
    double cooldownSeconds = 1.0;
    // Launch speed in m/s (the projectile's damage grows with it).
    double speed = 25.0;
    // Random deviation of a shot from its aim, in degrees (the spread of the reference's archers on
    // normal difficulty; this game has no difficulty).
    double spreadDegrees = 1.5;
    // It shoots at targets up to `range` metres away; it backs off from one closer than
    // `minDistance` and walks towards one farther than `keepDistance` (or one it does not see),
    // circling sideways in between. All three are horizontal distances.
    double range = 15.0;
    double minDistance = 5.0;
    double keepDistance = 12.0;

    bool operator==(const QcArcherDefinition &) const = default;
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
    // What decides what it does (decision E-423): QcMobBehaviour keeps one brain of each kind.
    // Hostile ones chase players (only with `hostile`), passive ones wander and flee, villagers live
    // by their villager settings (only with `villager`), archers keep their distance and shoot
    // (only with `hostile` and `archer`, decision E-426), neutral ones wander like passive ones until a
    // player provokes them (hurts them, or looks at their head with `gaze`) and then chase that player
    // (decision E-460). Server side only.
    enum class Ai : quint8 { Passive, Hostile, Villager, Archer, Neutral };
    Ai ai = Ai::Passive;
    QcMobAbilities abilities;
    // How an archer shoots (Ai::Archer).
    QcArcherDefinition archer;
    // Provoked by a player's look at its head (Ai::Neutral, decision E-460).
    std::optional<QcGazeDefinition> gaze;
    // Teleports away (decision E-460).
    std::optional<QcTeleportDefinition> teleport;
    // Takes and places single blocks (decision E-460).
    std::optional<QcCarryDefinition> carries;
    // Damage it takes every QcBodyBurning::wetIntervalTicks in water or rain (decision E-460); 0 for
    // none. Server side only.
    double waterDamage = 0.0;
    // Catches fire in direct sunlight (decision E-426): by day, with the open sky above its head,
    // neither in water nor in the rain. Server side only.
    bool burnsInDaylight = false;
    // Item it holds in its right hand (an archer's bow), shown on the client with the entity's held
    // item; empty for none. Server side only.
    QString heldItem;
    // The id of heldItem, resolved when the content is finished (QcContentRegistry::finish).
    QcItemId heldItemId = qcNoItem;
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
    // Skeleton mobs only: a cape on the back (an appearance of kind cape); empty for none.
    QString cape;
    // Skeleton mobs that walk with their arms stretched forward (zombies).
    bool armsForward = false;
    // A villager (decision E-411): lives in a village with a bed and a workstation, follows the
    // routine of its villager settings and opens doors; it is drawn in the look the server sends
    // with it (QcMsg::EntitySpawn::appearance) rather than in `outfit`.
    bool villager = false;
    // The villager settings it lives by (villagers/<name>.json, decision E-421); set exactly when
    // `villager` is. Server side only.
    QString villagerSettings;
    // The index of those settings in QcContentRegistry::villagerSpecies (QcContentRegistry::finish),
    // -1 before.
    int villagerSettingsIndex = -1;
    QList<QcItemDrop> drops;
    // Experience points a creature killed by a player gives as orbs (decision E-398). Server side
    // only.
    QcExperienceDrop experience;
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
    // Feeding and young ones (decision E-293); only `breeding.food` reaches the client.
    QcBreedingDefinition breeding;

    static constexpr int maxGlow = 15;
};
