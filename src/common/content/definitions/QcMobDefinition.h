#pragma once

#include "common/content/definitions/QcAppearanceDefinition.h"
#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/definitions/QcMobTraits.h"
#include "common/core/QcVec3.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcStatusEffects.h"

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
    // Tinted with a colour of the creature (QcTint::Collar: the collar of a tame one, decision E-480);
    // None for the texture as it is.
    QcTint tint = QcTint::None;
    // Where the box moves while the creature sits (QcPose::Sitting): a sitting dog lowers its back
    // and folds its hind legs.
    QcVec3 sitOffset = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
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
    // Stays under water (drowned, decision E-838): it sinks and walks on the bottom and swims up
    // only towards a target above it or at a wall, instead of keeping its head above the surface.
    bool dives = false;

    bool operator==(const QcMobAbilities &) const = default;
};

// How a creature with the golem AI fights, is repaired and built (decision E-471), from its data
// (`golem`). Server side only.
struct QcGolemDefinition
{
    // Share of a blow's push it shrugs off (1: it stands firm).
    double knockbackResistance = 0.0;
    // Its blows throw what they strike upwards this many times a blow's usual lift.
    double lift = 1.0;
    // Played where a blow of it lands; empty for none.
    QString attackSound;
    // Used on it (one of the held stack), this item heals `repairHealth` with `repairSound`; empty:
    // it is not repaired.
    QString repairItem;
    double repairHealth = 25.0;
    QString repairSound;
    // Players build it from four blocks of `buildBody` in a T standing upright with `buildHead`
    // placed on the T's middle last; empty: it is not built.
    QString buildBody;
    QString buildHead;

    bool operator==(const QcGolemDefinition &) const = default;
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
    // (decision E-460), golems guard villagers against hostile creatures (only without `hostile`, with
    // `golem`, decision E-471), guardians keep to the zone they guard and chase only players who
    // come within `guardLeash` of it (only with `hostile`, decision E-594). Server side only.
    enum class Ai : quint8 { Passive, Hostile, Villager, Archer, Neutral, Golem, Guardian };
    Ai ai = Ai::Passive;
    QcMobAbilities abilities;
    // How an archer shoots (Ai::Archer).
    QcArcherDefinition archer;
    // How a golem fights, is repaired and built (Ai::Golem).
    QcGolemDefinition golem;
    // Provoked by a player's look at its head (Ai::Neutral, decision E-460).
    std::optional<QcGazeDefinition> gaze;
    // Teleports away (decision E-460).
    std::optional<QcTeleportDefinition> teleport;
    // Takes and places single blocks (decision E-460).
    std::optional<QcCarryDefinition> carries;
    // Crumbles into blocks at a liquid's source (decision E-593).
    std::optional<QcCrumbleDefinition> crumbles;
    // Drinks to heal itself (the witch, decision E-623).
    std::optional<QcDrinkHealDefinition> drinks;
    // Splits off children when hurt and takes them with it when it dies (the Kjut, decision E-839).
    std::optional<QcFamilyDefinition> family;
    // Hovers above the ground instead of walking (the Kjut, decision E-839).
    std::optional<QcHoverDefinition> hover;
    // A guardian (Ai::Guardian) chases players up to this many blocks outside the zone it guards
    // (`"guardian": {"leash": 6}`, decision E-594). Server side only.
    double guardLeash = 6.0;
    // Damage it takes every QcBodyBurning::wetIntervalTicks in water or rain (decision E-460); 0 for
    // none. Server side only.
    double waterDamage = 0.0;
    // Players tame it (decision E-480).
    std::optional<QcTameDefinition> tameable;
    // It flees from players who come close (decision E-480).
    std::optional<QcShyDefinition> shy;
    // A neutral creature struck by a player angers the creatures of its kind within this many metres
    // at that player (a wolf pack, decision E-480); 0 for none. Server side only.
    double packRange = 0.0;
    // Creatures with `scaredOfCats` flee from creatures that `frighten` (cats, decision E-480). No
    // creature of the base game is scared of cats yet (the reference's creeper is missing). Server side
    // only.
    bool frightens = false;
    bool scaredOfCats = false;
    // Catches fire in direct sunlight (decision E-426): by day, with the open sky above its head,
    // neither in water nor in the rain. Server side only.
    bool burnsInDaylight = false;
    // Kinds of status effects that do nothing to it (spiders and the undead: poison; decision E-450).
    // Server side only.
    QStringList immuneTo;
    // Effects its blows give the player struck (a cave spider's poison), with their ticks. Server
    // side only.
    QList<QcStatusEffect> blowEffects;
    // A hostile creature that leaves players alone by day unless one hurt it (spiders, decision
    // E-451). Server side only.
    bool neutralByDay = false;
    // Leaps at its target from QcMobSteering::leapMin to leapMax metres with this horizontal speed
    // in m/s (spiders); 0 for never. Server side only.
    double leapSpeed = 0.0;
    // Falls no faster than this, in m/s, at least one tick of gravity (a chicken flapping its wings);
    // 0 for no limit. Server side only.
    double maxFallSpeed = 0.0;
    // Hides in blocks (silverfish, decision E-453): an idle one turns a block next to it into the
    // infested block of its kind and is gone; one that is hurt wakes its kind in the infested blocks
    // around it (QcInfestation). Server side only.
    bool hidesInBlocks = false;
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
    // Brightest block light (torches) at the feet that still allows spawning, 0..15 (hostile
    // creatures: 0 unless the data says otherwise).
    int maxSpawnLight = 15;
    // The monsters' light test of the original game (QcSpawnLight, decision E-836): a random draw
    // against the sky light and the darkened sky; on by default for hostile creatures.
    bool spawnInDarkness = false;
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
    // The index of those settings in QcVillageCatalog::villagerSpecies (QcContentRegistry::finish),
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
    // Only where its feet see the open sky (sky light 15: husks, strays; decision E-838).
    bool spawnUnderSky = false;
    // In water instead of on dry ground: its feet in water above a solid block (drowned; decision
    // E-838). Server side only.
    bool spawnInWater = false;

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
        bool darkness = false;
        bool sky = false;
        bool water = false;
    };
    QList<SpawnRule> extraSpawns;
    // The main rule followed by the extra ones.
    QList<SpawnRule> spawnRules() const
    {
        QList<SpawnRule> rules{{spawnTime, spawnBlocks, spawnWeight, spawnsInCaves, maxSpawnLight, spawnChunkChance, spawnMaxY, spawnBiomes,
                                 spawnInDarkness, spawnUnderSky, spawnInWater}};
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
    // Whether creatures of the kind spawn as monsters (decision E-836): hostile ones that walk, hover
    // or fly, not water or ambient creatures.
    bool isMonster() const { return hostile && !aquatic && !ambient; }
    // Using `catchWith` on the creature turns one of those items into `catchItem` and removes it,
    // with `catchSound` (server side only).
    QString catchWith;
    QString catchItem;
    QString catchSound;
    // Feeding and young ones (decision E-293); only `breeding.food` reaches the client.
    QcBreedingDefinition breeding;
    // Laying eggs (decision E-452). Server side only.
    QcLayingDefinition laying;

    static constexpr int maxGlow = 15;
    // Lives in water and schools (fish, decision E-502).
    std::optional<QcAquaticDefinition> aquatic;
    // Using `milkWith` on a grown one turns one of those items into `milkItem` with `milkSound`; the
    // creature stays (a cow and a bucket, decision E-500). `milkWith` reaches the client.
    QString milkWith;
    QString milkItem;
    QString milkSound;
};
