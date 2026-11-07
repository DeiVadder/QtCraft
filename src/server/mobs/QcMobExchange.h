// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"
#include "common/core/QcCreatureKindId.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcStatusEffects.h"
#include "server/animals/QcBarterState.h"
#include "server/entities/QcEntityComponent.h"
#include "server/mobbrains/QcTameState.h"
#include "server/villages/QcVillager.h"

#include <QList>
#include <QString>

#include <optional>

/*! \brief The data the creatures exchange with the rest of the server (decision E-422): what they
    know of the players (PlayerView), the blows and block changes their brains decide in a tick and
    the server carries out after it (PlayerHit, CreatureHit, BlockAction), and a persistent creature
    as saved with the world (Stored).

    QcMobManager names them as its own members. */
namespace QcMobExchange {

/*! \brief A persistent mob as saved with the world. */
struct Stored
{
    /*! Its creature kind (index in the content registry). */
    QcCreatureKindId type;
    /*! Where its feet are. */
    QcVec3 position;
    /*! Where it faces (degrees). */
    float yaw = 0.0f;
    /*! Its health points. */
    double health = 0.0;
    /*! Seconds until it can breed again (QcMobBreeding). */
    double breedCooldown = 0.0;
    /*! Seconds until a young one grows up. */
    double growUp = 0.0;
    /*! Whether it was bred by a player. */
    bool bred = false;
    /*! A villager's home, work and profession (decision E-411). */
    std::optional<QcVillagerRecord> villager;
    /*! Its persistent id (QcCreature::uid) and what its entry holds that this version does
        not use (decision E-420). \qcshared */
    quint64 uid = 0;
    QcEntityKept kept = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! Its status effects (decision E-450). */
    QList<QcStatusEffect> effects = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! Seconds until it lays next (QcCreature::layTimer, decision E-452); below 0 while not
        chosen. */
    double layTimer = -1.0;
    /*! A golem a player built (decision E-471). */
    bool playerBuilt = false;
    /*! The block it carries, whether it is kept only for that block and whether it stays in its
        enclosure (decision E-460). \qcshared */
    QcBlockId carried = qcAirBlockId;
    bool keptForBlock = false;
    bool confined = false;
    /*! Wears a saddle (decision E-1054). */
    bool saddled = false;
    /*! Without a mind (QcCreature::noAi, decision E-510). */
    bool noAi = false;
    /*! Owner, sitting and collar of a tame one (decision E-480; its target is not saved). */
    std::optional<QcTameState> tame = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! The persistent id of its parent when it is a child of a kept family (decision E-839); 0
        otherwise. */
    quint64 parentUid = 0;
    /*! The chunk that parent was in when the child last saw it (QcCreature::parentChunk). */
    std::optional<QcChunkPos> parentChunk = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! The name a name tag gave it (QcCreature::name, decision E-866). */
    QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! What it holds and looks at when it barters (QcCreature::barter, decision E-1081); none
        when it holds nothing. */
    std::optional<QcBarterState> barter = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

    bool operator==(const Stored &) const = default;
};

/*! \brief What the mob logic needs to know about a player. */
struct PlayerView
{
    /*! The player's entity id. */
    quint32 entityId = 0;
    /*! Where the player's feet are. */
    QcVec3 position;
    /*! A living body in the world: creatures notice only these (a spectator has none, decision
        E-730), and only these keep creatures from despawning and monsters from spawning close by
        (decision E-836). */
    bool alive = true;
    /*! Creatures leave invulnerable players (creative mode) alone. */
    bool targetable = true;
    /*! Where the player looks (a unit vector; zero for unknown): a look at a head provokes
        neutral creatures with a gaze (decision E-460). */
    QcVec3 look = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! Villages hold their grudges by name (QcGolemGrudges). */
    QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! The persistent identity of the player (QcPetOwners::keyOf): tame creatures know their
        owner by it (decision E-480). */
    QString identity = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! Entity ids of the creature that hurt the player and of the one the player struck lately
        (0: none): pets that defend attack them. \qcshared */
    quint32 hurtBy = 0;
    quint32 struck = 0;
    /*! The item in the player's hand (shy creatures). */
    QcItemId held = qcNoItem;
    /*! The armour the player wears, one item per piece (creatures that spare who wears gold,
        QcBarterDefinition::sparedBy, decision E-1081). */
    QList<QcItemId> worn = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! Its status effects (an archer's choice of projectile, decision E-623); null for none
        known. */
    const QcStatusEffects *effects = nullptr;
    /*! Its health (an archer's choice of projectile, decision E-1105). */
    double health = 20.0;
    /*! The player's horizontal speed in m/s (shy creatures). */
    double speed = 0.0;
    /*! Server ticks at which hurtBy and struck happened: a pet keeps its target unless its owner
        fights a foe anew (decision E-480). \qcshared */
    quint64 hurtTick = 0;
    quint64 struckTick = 0;
    /*! Rides a minecart or drives a boat: the vehicle meets the creatures (decisions E-382,
        E-920), the player's body does not push them (decision E-882). */
    bool seated = false;
    /*! The share of a creature's sight range within which it notices this player
        (QcEffectRules::sightShare, decision E-992): 1 unless he is invisible. */
    double sightShare = 1.0;
};

/*! \brief A block a creature takes or puts down in a tick (a shadewalker, decision E-460), which
    the server carries out after the tick with the usual block changes (QcCreatureTraits): taking
    only when \c {block} is still there, placing only into air; dropping (game rule mobGriefing
    off) lets the carried block fall as an item. */
struct BlockAction
{
    /*! \brief What the creature does with the block. */
    enum class Kind : quint8 {
        /*! It takes the block. */
        Take,
        /*! It puts its carried block down. */
        Place,
        /*! It lets its carried block fall as an item. */
        Drop
    };
    /*! What it does. */
    Kind kind = Kind::Take;
    /*! Entity id of the creature. */
    quint32 mob = 0;
    /*! Where the block is or goes. */
    QcBlockPos pos;
    /*! The block taken or put down. */
    QcBlockId block = qcAirBlockId;
};

/*! \brief A blow of a creature at a player; the server deals it after the tick. */
struct PlayerHit
{
    /*! Entity id of the player hit. */
    quint32 playerEntityId = 0;
    /*! The damage before armour. */
    double damage = 0.0;
    /*! The push the player gets (m/s). */
    QcVec3 knockback;
    /*! Times a blow's usual lift (a golem's throws upwards, decision E-471). */
    double lift = 1.0;
    /*! The kind of the creature that struck; its blow effects go with the hit (decision E-450). */
    std::optional<QcCreatureKindId> mobType;
    /*! Entity id of the creature that struck (0: unknown); the player's pets turn on it
        (decision E-480). */
    quint32 mob = 0;
};

/*! \brief A blow of one creature at another (a golem at a hostile creature, decision E-471; a pet
    at its owner's foe, decision E-480); the server deals it after the tick (QcServerCreatures). */
struct CreatureHit
{
    /*! Entity id of the creature that strikes. */
    quint32 attacker = 0;
    /*! Entity id of the creature struck. */
    quint32 target = 0;
    /*! The damage of the blow. */
    double damage = 0.0;
    /*! Where the blow comes from (the push goes away from it). */
    QcVec3 from;
    /*! Times a blow's usual lift (PlayerHit::lift). */
    double lift = 1.0;
};

} // namespace QcMobExchange
