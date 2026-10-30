// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/items/QcItemDefinition.h"
#include "common/content/mobs/QcAppearanceDefinition.h"
#include "common/content/mobs/QcMobTraits.h"
#include "common/core/QcVec3.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcStatusEffects.h"

#include <QList>
#include <QString>

#include <algorithm>
#include <optional>

/*! \brief One box of a mob's model, in metres relative to the mob's feet centre, facing north
    (-Z). */
struct QcMobPart
{
    /*! Lower corner of the box (\c {"from"}), below \c {to} on every axis. */
    QcVec3 from;
    /*! Upper corner of the box (\c {"to"}). */
    QcVec3 to;
    /*! Content name of the texture on the box (\c {"texture"}). */
    QString texture;
    /*! Tinted with a colour of the creature (QcTint::Collar: the collar of a tame one, decision
        E-480); None for the texture as it is. */
    QcTint tint = QcTint::None;
    /*! Where the box moves while the creature sits (QcPose::Sitting): a sitting dog lowers its
        back and folds its hind legs. */
    QcVec3 sitOffset = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
};

/*! \brief How a creature finds its way (decision E-423), from its data (\c {abilities}); the path
    rules (QcPathRules::of) follow from it.

    Server side only. */
struct QcMobAbilities
{
    /*! How it climbs (\c {"climbs"}: none, ladders or walls). */
    enum class Climbing : quint8 {
        /*! Does not climb. */
        None,
        /*! Holds on to ladders and goes up and down along them. */
        Ladders,
        /*! Also up walls (spiders, stage 52); on paths like Ladders until then. */
        Walls,
    };

    /*! Opens closed wooden doors and gates on its way (only the villager AI does so far). */
    bool opensDoors = false;
    /*! How it climbs. */
    Climbing climbs = Climbing::None;
    /*! Moves through water on its paths as freely as on land; a walker uses water only without a
        dry way. */
    bool swims = false;
    /*! Drops deeper than the usual limit into deep water (QcPathRules::waterDrop). */
    bool divesIntoWater = true;
    /*! Stays under water (drowned, decision E-838): it sinks and walks on the bottom and swims
        up only towards a target above it or at a wall, instead of keeping its head above the
        surface. */
    bool dives = false;

    bool operator==(const QcMobAbilities &) const = default;
};

/*! \brief How a creature with the golem AI fights, is repaired and built (decision E-471), from its
    data (\c {golem}).

    Server side only. */
struct QcGolemDefinition
{
    /*! Share of a blow's push it shrugs off (1: it stands firm). */
    double knockbackResistance = 0.0;
    /*! Its blows throw what they strike upwards this many times a blow's usual lift. */
    double lift = 1.0;
    /*! Played where a blow of it lands; empty for none. */
    QString attackSound;
    /*! Used on it (one of the held stack), \c {repairItem} heals \c {repairHealth} with
        \c {repairSound}; an empty \c {repairItem}: it is not repaired. \qcshared */
    QString repairItem;
    double repairHealth = 25.0;
    QString repairSound;
    /*! Players build it from four blocks of \c {buildBody} in a T standing upright with
        \c {buildHead} placed on the T's middle last (\c {"build": {"body", "head"}}); empty:
        it is not built. \qcshared */
    QString buildBody;
    QString buildHead;

    bool operator==(const QcGolemDefinition &) const = default;
};

/*! \brief Data definition of a creature: stats, behaviour, spawning, model and drops. */
struct QcMobDefinition
{
    /*! Time of day a spawn rule allows (\c {"time"}: any, day or night). */
    enum class SpawnTime {
        Any, // day and night
        Day, // only by day
        Night // only at night
    };

    /*! Content name of the creature (\c {"name"}, qualified with the pack's namespace). */
    QString name;
    /*! Translation key of its name (\c {"translation"}, else derived from the name). */
    QString translationKey;
    /*! Translation key of the handbook's text about it ("<translation>.description" unless set);
        a missing text leaves only the facts. Server side only. */
    QString descriptionKey;
    /*! Full health (\c {"health"}, 1..1000). */
    double maxHealth = 10.0;
    /*! Width of its body box in metres (\c {"width"}, 0.2..4). */
    double width = 0.6;
    /*! Height of its body box in metres (\c {"height"}, 0.2..4). */
    double height = 1.8;
    /*! Movement speed in m/s while wandering; chasing uses chaseSpeed. */
    double walkSpeed = 1.5;
    /*! Movement speed in m/s while chasing (\c {"chaseSpeed"}, twice \c {walkSpeed} unless
        set). */
    double chaseSpeed = 3.5;
    /*! Hostile to players (\c {"hostile"}): the default AI is then Ai::Hostile, and the hostile,
        archer and guardian AIs require it. */
    bool hostile = false;
    /*! \brief What decides what it does (decision E-423): QcMobBehaviour keeps one brain of each
        kind.

        Hostile ones chase players (only with \c {hostile}), passive ones wander and flee,
        villagers live by their villager settings (only with \c {villager}), archers keep their
        distance and shoot (only with \c {hostile} and \c {archer}, decision E-426), neutral ones
        wander like passive ones until a player provokes them (hurts them, or looks at their head
        with \c {gaze}) and then chase that player (decision E-460), golems guard villagers
        against hostile creatures (only without \c {hostile}, with \c {golem}, decision E-471),
        guardians keep to the zone they guard and chase only players who come within
        \c {guardLeash} of it (only with \c {hostile}, decision E-594). Server side only. */
    enum class Ai : quint8 {
        Passive, // wanders and flees
        Hostile, // chases players
        Villager, // lives by its villager settings
        Archer, // keeps its distance and shoots
        Neutral, // passive until a player provokes it
        Golem, // guards villagers
        Guardian // guards a zone
    };
    /*! Its AI (\c {"ai"}); without the key: Villager for villagers, Hostile for hostile
        creatures, else Passive. */
    Ai ai = Ai::Passive;
    /*! How it finds its way (\c {"abilities"}); the villager AI starts from opening doors and
        climbing ladders. */
    QcMobAbilities abilities;
    /*! How an archer shoots (Ai::Archer). */
    QcArcherDefinition archer;
    /*! How a golem fights, is repaired and built (Ai::Golem). */
    QcGolemDefinition golem;
    /*! Provoked by a player's look at its head (Ai::Neutral, decision E-460). */
    std::optional<QcGazeDefinition> gaze;
    /*! Teleports away (decision E-460). */
    std::optional<QcTeleportDefinition> teleport;
    /*! Takes and places single blocks (decision E-460). */
    std::optional<QcCarryDefinition> carries;
    /*! Crumbles into blocks at a liquid's source (decision E-593). */
    std::optional<QcCrumbleDefinition> crumbles;
    /*! Drinks to heal itself (the witch, decision E-623). */
    std::optional<QcDrinkHealDefinition> drinks;
    /*! Splits off children when hurt and takes them with it when it dies (the Kjut, decision
        E-839). */
    std::optional<QcFamilyDefinition> family;
    /*! Hovers above the ground instead of walking (the Kjut, decision E-839). */
    std::optional<QcHoverDefinition> hover;
    /*! A guardian (Ai::Guardian) chases players up to this many blocks outside the zone it
        guards (\c {"guardian": {"leash": 6}}, decision E-594). Server side only. */
    double guardLeash = 6.0;
    /*! Damage it takes every QcBodyBurning::wetIntervalTicks in water or rain (decision E-460);
        0 for none. Server side only. */
    double waterDamage = 0.0;
    /*! Players tame it (decision E-480). */
    std::optional<QcTameDefinition> tameable;
    /*! It flees from players who come close (decision E-480). */
    std::optional<QcShyDefinition> shy;
    /*! A neutral creature struck by a player angers the creatures of its kind within this many
        metres at that player (a wolf pack, decision E-480); 0 for none. Server side only. */
    double packRange = 0.0;
    /*! Creatures with \c {scaredOfCats} flee from creatures that \c {frightens} (cats, decision
        E-480). No creature of the base game is scared of cats yet (the reference's creeper is
        missing). Server side only. \qcshared */
    bool frightens = false;
    bool scaredOfCats = false;
    /*! Catches fire in direct sunlight (decision E-426): by day, with the open sky above its
        head, neither in water nor in the rain. Server side only. */
    bool burnsInDaylight = false;
    /*! Kinds of status effects that do nothing to it (spiders and the undead: poison; decision
        E-450). Server side only. */
    QStringList immuneTo;
    /*! Effects its blows give the player struck (a cave spider's poison), with their ticks.
        Server side only. */
    QList<QcStatusEffect> blowEffects;
    /*! A hostile creature that leaves players alone by day unless one hurt it (spiders, decision
        E-451). Server side only. */
    bool neutralByDay = false;
    /*! Leaps at its target from QcMobSteering::leapMin to leapMax metres with this horizontal
        speed in m/s (spiders); 0 for never. Server side only. */
    double leapSpeed = 0.0;
    /*! Falls no faster than this, in m/s, at least one tick of gravity (a chicken flapping its
        wings); 0 for no limit. Server side only. */
    double maxFallSpeed = 0.0;
    /*! Hides in blocks (silverfish, decision E-453): an idle one turns a block next to it into
        the infested block of its kind and is gone; one that is hurt wakes its kind in the
        infested blocks around it (QcInfestation). Server side only. */
    bool hidesInBlocks = false;
    /*! Item it holds in its right hand (an archer's bow), shown on the client with the entity's
        held item; empty for none. Server side only. */
    QString heldItem;
    /*! The id of heldItem, resolved when the content is finished (QcContentRegistry::finish). */
    QcItemId heldItemId = qcNoItem;
    /*! Damage of one of its blows (\c {"attackDamage"}, 0..100). */
    double attackDamage = 2.0;
    /*! Horizontal distance in metres from which it strikes its target (\c {"attackRange"},
        0.5..8). */
    double attackRange = 1.6;
    /*! Seconds between two of its blows (\c {"attackCooldown"}, 0.1..60). */
    double attackCooldown = 1.0;
    /*! Distance in metres within which it notices players (\c {"sightRange"}, 1..64). */
    double sightRange = 16.0;
    /*! Spawning (in \c {"spawn"}): time of day (\c {"time"}), blocks the mob may spawn on
        (\c {"blocks"}; empty: any solid), most creatures in a group (\c {"maxGroup"}, 1..4) and
        relative weight among the mobs that may spawn at a place (\c {"weight"}, 0..100).
        \qcshared */
    SpawnTime spawnTime = SpawnTime::Any;
    QList<QString> spawnBlocks;
    int maxGroup = 2;
    double spawnWeight = 1.0;
    /*! Hostile mobs that need darkness also spawn underground at any time of day. */
    bool spawnsInCaves = false;
    /*! Brightest block light (torches) at the feet that still allows spawning, 0..15 (hostile
        creatures: 0 unless the data says otherwise). */
    int maxSpawnLight = 15;
    /*! The monsters' light test of the original game (QcSpawnLight, decision E-836): a random
        draw against the sky light and the darkened sky; on by default for hostile creatures. */
    bool spawnInDarkness = false;
    /*! Share of chunks (chosen by the world seed, separately for each kind) where it spawns at
        all, like slime chunks; 1 for every chunk. Server side only. */
    double spawnChunkChance = 1.0;
    /*! Highest block its feet may stand in when spawning. Server side only. */
    int spawnMaxY = QcWorld::worldHeight;
    /*! The model: either static boxes (\c {parts}, from \c {"model"}) or an animated skeleton
        model (\c {skeleton}) with an outfit (\c {outfit}); \c {skeleton} and \c {outfit} go
        together. \qcshared */
    QList<QcMobPart> parts;
    QString skeleton;
    QString outfit;
    /*! Skeleton mobs only: a cape on the back (an appearance of kind cape); empty for none. */
    QString cape;
    /*! Skeleton mobs that walk with their arms stretched forward (zombies). */
    bool armsForward = false;
    /*! A villager (decision E-411): lives in a village with a bed and a workstation, follows the
        routine of its villager settings and opens doors; it is drawn in the look the server
        sends with it (QcMsg::EntitySpawn::appearance) rather than in \c {outfit}. */
    bool villager = false;
    /*! The villager settings it lives by (villagers/<name>.json, decision E-421); set exactly
        when \c {villager} is. Server side only. */
    QString villagerSettings;
    /*! The index of those settings in QcVillageCatalog::villagerSpecies
        (QcContentRegistry::finish), -1 before. */
    int villagerSettingsIndex = -1;
    /*! Items it drops when killed (\c {"drops"}: item, count 1..64, chance 0..1). */
    QList<QcItemDrop> drops;
    /*! Experience points a creature killed by a player gives as orbs (decision E-398). Server
        side only. */
    QcExperienceDrop experience;
    /*! Sounds (in \c {"sounds"}): now and then while alive (\c {"ambient"}), when hit
        (\c {"hurt"}) and when killed (\c {"death"}); empty names are silent. Hurt and death
        sounds fall back to the common creature sounds. \qcshared */
    QString ambientSound;
    QString hurtSound;
    QString deathSound;
    /*! Flies (no gravity) and drifts a little above the ground. */
    bool flying = false;
    /*! Drawn at least this bright (0..15): glowing creatures. It lights nothing around them. */
    int glow = 0;
    /*! Ambient creatures (fireflies) have their own spawn cap and never count as passive or
        hostile. */
    bool ambient = false;
    /*! Most creatures of this kind near one player; 0 for no limit beyond the category's cap. */
    int maxCount = 0;
    /*! Biomes it spawns in; empty for all. */
    QStringList spawnBiomes;
    /*! Only where its feet see the open sky (sky light 15: husks, strays; decision E-838). */
    bool spawnUnderSky = false;
    /*! In water instead of on dry ground: its feet in water above a solid block (drowned;
        decision E-838). Server side only. */
    bool spawnInWater = false;

    /*! \brief One place and time where the creature spawns: the main rule (the fields above) or an
        extra one (\c {spawn.also}, e.g. slimes also on swamp surfaces at night).

        Server side only. */
    struct SpawnRule
    {
        /*! Time of day (\c {"time"}), as spawnTime. */
        SpawnTime time = SpawnTime::Any;
        /*! Blocks it may spawn on (\c {"blocks"}), as spawnBlocks. */
        QList<QString> blocks;
        /*! Relative weight (\c {"weight"}), as spawnWeight. */
        double weight = 1.0;
        /*! Also underground at any time of day (\c {"caves"}), as spawnsInCaves. */
        bool caves = false;
        /*! Brightest block light at the feet (\c {"maxLight"}), as maxSpawnLight. */
        int maxLight = 15;
        /*! Share of chunks where it spawns (\c {"chunkChance"}), as spawnChunkChance. */
        double chunkChance = 1.0;
        /*! Highest block its feet may stand in (\c {"maxY"}), as spawnMaxY. */
        int maxY = QcWorld::worldHeight;
        /*! Biomes it spawns in (\c {"biomes"}), as spawnBiomes. */
        QStringList biomes;
        /*! The monsters' light test (\c {"darkness"}), as spawnInDarkness. */
        bool darkness = false;
        /*! Only under the open sky (\c {"sky"}), as spawnUnderSky. */
        bool sky = false;
        /*! In water (\c {"water"}), as spawnInWater. */
        bool water = false;
    };
    /*! Extra spawn rules (\c {"spawn": {"also": [...]}}), at most maxExtraSpawns. */
    QList<SpawnRule> extraSpawns;
    /*! Returns the main rule followed by the extra ones. */
    QList<SpawnRule> spawnRules() const
    {
        QList<SpawnRule> rules{{spawnTime, spawnBlocks, spawnWeight, spawnsInCaves, maxSpawnLight, spawnChunkChance, spawnMaxY, spawnBiomes,
                                 spawnInDarkness, spawnUnderSky, spawnInWater}};
        rules.append(extraSpawns);
        return rules;
    }
    /*! Most extra spawn rules a creature may have. */
    static constexpr int maxExtraSpawns = 8;
    /*! Returns whether any rule lets it spawn by itself; with all weights 0 it only comes from
        /summon and world setups (the showcase's mannequin). */
    bool spawnsNaturally() const
    {
        return spawnWeight > 0.0 || std::ranges::any_of(extraSpawns, [](const SpawnRule &rule) { return rule.weight > 0.0; });
    }
    /*! Returns whether creatures of the kind spawn as monsters (decision E-836): hostile ones
        that walk, hover or fly, not water or ambient creatures. */
    bool isMonster() const { return hostile && !aquatic && !ambient; }
    /*! Using \c {catchWith} on the creature turns one of those items into \c {catchItem} and
        removes it, with \c {catchSound} (\c {"catch": {"with", "item", "sound"}}; server side
        only). \qcshared */
    QString catchWith;
    QString catchItem;
    QString catchSound;
    /*! Feeding and young ones (decision E-293); only \c {breeding.food} reaches the client. */
    QcBreedingDefinition breeding;
    /*! Laying eggs (decision E-452). Server side only. */
    QcLayingDefinition laying;

    /*! Highest value of \c {glow}. */
    static constexpr int maxGlow = 15;
    /*! Lives in water and schools (fish, decision E-502). */
    std::optional<QcAquaticDefinition> aquatic;
    /*! Using \c {milkWith} on a grown one turns one of those items into \c {milkItem} with
        \c {milkSound}; the creature stays (a cow and a bucket, decision E-500). \c {milkWith}
        reaches the client (\c {"milk": {"with", "item", "sound"}}). \qcshared */
    QString milkWith;
    QString milkItem;
    QString milkSound;
};
