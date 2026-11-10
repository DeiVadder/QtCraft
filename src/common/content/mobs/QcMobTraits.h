// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcCreatureKindId.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief A creature that a player provokes by looking at its head (the shadewalker, decision
    E-460), from its data (\c {gaze}): the player's look and the direction from their eyes to the
    middle of its head agree within \c {tolerance}, which narrows with the distance d (look ·
    direction > 1 − tolerance / d, like the reference game: about 4.5° at 8 m, 1.8° at 50 m), from
    no farther than \c {range} metres, with nothing in between.

    Server side only. \qcfields */
struct QcGazeDefinition
{
    double tolerance = 0.025;
    double range = 64.0;
    /*! Where its head is, as a share of its height (the middle of the head). */
    double headShare = 0.9;

    bool operator==(const QcGazeDefinition &) const = default;
};

/*! \brief A creature that crumbles into blocks where it touches a source block of the liquid
    \c {liquid} (the sand golem, decision E-593), from its data (\c {crumbles}): it is gone without
    loot or experience, and its build stands in its place, an iron golem's T in reverse: \c {body}
    at its feet, in the middle above them and on both sides of the middle across its facing,
    \c {head} on the middle.

    Each block goes into an open cell (air, a plant, flowing liquid; never a source); where none
    is open the block drops as an item. Server side only. \qcfields */
struct QcCrumbleDefinition
{
    QString liquid;
    QString body;
    QString head;
    /*! Played where it crumbles; empty for none. */
    QString sound;

    bool operator==(const QcCrumbleDefinition &) const = default;
};

/*! \brief A creature that teleports (the shadewalker, decision E-460), from its data
    (\c {teleport}): to a random safe place within \c {range} metres (a solid floor, room for its
    body, no liquid, only in loaded chunks; QcSafeSpot), at most once per \c {cooldownSeconds}.

    Server side only. \qcfields */
struct QcTeleportDefinition
{
    double range = 32.0;
    /*! Random places tried per teleport before it gives up. */
    int attempts = 16;
    /*! Seconds after a teleport before the next one. */
    double cooldownSeconds = 0.5;
    /*! Projectiles do not hurt it; it teleports away instead (and the projectile bounces off). */
    bool dodgesProjectiles = true;
    /*! It teleports away while it is in water or the rain falls on it. */
    bool whenWet = true;
    /*! Chance per second that an angry one far from its target teleports towards it. */
    double towardsTargetChance = 0.05;
    /*! Sound key played where it leaves and where it arrives; empty for silence. */
    QString sound;

    bool operator==(const QcTeleportDefinition &) const = default;
};

/*! \brief A creature that takes single blocks and puts them down again (the shadewalker, decision
    E-460), from its data (\c {carries}): only the blocks of \c {blocks}, and only while the game
    rule mobGriefing is on.

    Server side only. */
struct QcCarryDefinition
{
    /*! Names of the blocks it takes (any state of them). */
    QStringList blocks;
    /*! Their ids (QcBlockId of the default state), resolved when the content is finished:
        names without a block and an item of their own, and blocks with a block entity or of two
        cells, are left out with a warning; with none left it carries nothing. */
    QList<QcBlockId> blockIds;
    /*! Chance per second to take a block while it carries none (\c {takeChance}), and to put one
        down while it does (\c {placeChance}). \qcshared */
    double takeChance = 0.05;
    double placeChance = 0.0015;
    /*! How far around its feet it looks for a block (horizontally; from the floor up to
        \c {reach} + 1 above its feet). */
    int reach = 2;

    /*! Most block names a carrier may list. */
    static constexpr int maxBlocks = 64;

    bool operator==(const QcCarryDefinition &) const = default;
};

/*! \brief A creature that players tame (wolves, cats; decision E-480), from its data
    (\c {tameable}): each of its taming \c {items} used on it tames it with \c {chance} (a tame one
    belongs to that player's identity, follows, sits when its owner uses it and fights for its owner
    with \c {defends}); a wolf \c {becomes} a dog.

    Server side only, except that the client knows a kind is tameable at all (it sends every use
    of one to the server). */
struct QcTameDefinition
{
    /*! Names of the items that tame it; empty for a kind that is only ever tame (the dog). */
    QStringList items;
    /*! Chance that one item tames it. */
    double chance = 1.0 / 3.0;
    /*! The kind it turns into when tamed (a wolf into a dog); empty: it stays its kind. */
    QString becomes;
    /*! The kind \c {becomes} names among the content's mobs, resolved when the content is
        finished; none when it stays its kind. */
    std::optional<QcCreatureKindId> becomesType;
    /*! Food (\c {healItems}) that heals a tame one by \c {healAmount} (meat for dogs).
        \qcshared */
    QStringList healItems;
    double healAmount = 4.0;
    /*! A tame one that does not sit follows its owner when farther than \c {followDistance}
        metres, until it is within \c {stopDistance}, and teleports next to its owner when
        farther than \c {teleportDistance} (only to a safe place in loaded chunks; QcSafeSpot).
        \qcshared */
    double followDistance = 6.0;
    double stopDistance = 2.0;
    double teleportDistance = 12.0;
    /*! It attacks what hurts its owner and what its owner strikes (never its owner, never
        another pet of the same owner). */
    bool defends = false;
    /*! It wears a collar whose colour a dye of \c {collarDyes} changes to the entry of
        \c {collarColors} at the same index (0xRRGGBB); it starts with the colour of
        \c {collarDefault} (one of collarDyes). Without collarDyes it wears no collar
        (\c {"collar": {"default", "dyes"}}). \qcshared */
    QStringList collarDyes;
    QList<quint32> collarColors;
    QString collarDefault;

    /*! Most dyes a collar may take (\c {collarDyes}). */
    static constexpr int maxCollarDyes = 32;

    /*! Returns the colour of the dye \a name, if the collar takes it. */
    std::optional<quint32> collarColor(const QString &name) const
    {
        const qsizetype index = collarDyes.indexOf(name);
        return index < 0 ? std::nullopt : std::optional<quint32>(collarColors[index]);
    }
    /*! Returns whether it wears a collar (it has collar dyes). */
    bool hasCollar() const { return !collarDyes.isEmpty(); }

    bool operator==(const QcTameDefinition &) const = default;
};

/*! \brief A shy creature (the ocelot, decision E-480), from its data (\c {shy}): it flees from
    players closer than \c {distance} metres, except from one who holds one of its \c {calmItems}
    and moves no faster than \c {calmSpeed} m/s (sneaking up slowly with its food).

    Server side only. \qcfields */
struct QcShyDefinition
{
    double distance = 8.0;
    QStringList calmItems;
    double calmSpeed = 1.5;

    bool operator==(const QcShyDefinition &) const = default;
};

/*! \brief A creature that lives in water (fish, decision E-502), from its data (\c {aquatic}): it
    swims only in water, in schools with its kind (cohesion, alignment and separation among at most
    \c {neighbours} creatures of its kind within \c {schoolRadius} metres), flops about on land and
    suffocates there after \c {airSeconds}.

    Water creatures have their own caps and never count as animals: fish that of
    QcServerLimits::waterCreaturesPerPlayer, larger ones like the squid (\c {"cap": "large"},
    decision E-974) that of QcServerLimits::largeWaterCreaturesPerPlayer. Server side only. */
struct QcAquaticDefinition
{
    /*! The spawn cap a water creature counts against (\c {cap}). */
    enum class Cap : quint8 {
        /*! \c {"fish"} (default): small fish, QcServerLimits::waterCreaturesPerPlayer. */
        Fish,
        /*! \c {"large"}: the squid, QcServerLimits::largeWaterCreaturesPerPlayer. */
        Large,
    };

    /*! Speed in m/s while swimming; it darts away at the creature's chaseSpeed when hurt. */
    double swimSpeed = 1.5;
    /*! Seconds it lives out of water (\c {airSeconds}); after them it takes
        \c {suffocationDamage} every second. \qcshared */
    double airSeconds = 15.0;
    double suffocationDamage = 1.0;
    /*! Upward speed in m/s of a flop on land (sideways a third of it). */
    double flopSpeed = 4.0;
    /*! Metres within which creatures of its kind count for its school; 0 for no schooling. */
    double schoolRadius = 4.0;
    /*! Weight of the steering towards the centre of its school mates. */
    double cohesion = 1.5;
    /*! Weight of the steering along its school mates' mean heading. */
    double alignment = 0.8;
    /*! Weight of the steering away from school mates closer than \c {separationDistance}. */
    double separation = 1.2;
    /*! Distance in metres below which separation pushes it away from a school mate. */
    double separationDistance = 0.8;
    /*! Most school mates (the nearest) it steers by; 0 for no schooling. */
    int neighbours = 8;
    /*! The spawn cap it counts against. */
    Cap cap = Cap::Fish;

    bool operator==(const QcAquaticDefinition &) const = default;
};

/*! \brief A water creature that swims in pulses (the squid, decision E-974), from its data
    (\c {"aquatic": {"pulse": ...}}): each pulse of \c {seconds} starts with a push of
    \c {thrust} times the pulse towards \c {speed} m/s, then it glides and slows down; hurt, it
    pulses twice as often at its chaseSpeed.

    Reaches the client, which opens the tentacles (QcMobPart::tentacle) by up to \c {spread}
    degrees as the creature slows below \c {speed} and tilts the body along its way. */
struct QcPulseDefinition
{
    /*! Seconds of one pulse. */
    double seconds = 1.5;
    /*! Share of the pulse that pushes (0.05..0.9). */
    double thrust = 0.3;
    /*! Speed in m/s a push drives it to. */
    double speed = 2.0;
    /*! Degrees the tentacles open while it glides slowly or rests. */
    double spread = 40.0;

    bool operator==(const QcPulseDefinition &) const = default;
};

/*! \brief How a creature with the archer AI shoots (decision E-426), from its data (\c {archer}).

    Server side only. */
struct QcArcherDefinition
{
    /*! Name of the QcProjectileDefinition it shoots. */
    QString projectile;
    /*! \brief Another projectile it shoots instead while its target lacks a status effect (the
        witch's slowness, poison and weakness before its harming, decisions E-623, E-1105).

        \c {projectile} names the projectile, \c {unlessTargetHas} the status effect; the choice
        also needs the target at least \c {minDistance} and at most \c {maxDistance} metres away
        (horizontally), its health at least \c {minTargetHealth} and a draw below \c {chance}
        (JSON keys of the same names in \c {"choices"}). \qcfields */
    struct Choice
    {
        QString projectile;
        QString unlessTargetHas;
        double minDistance = 0.0;
        double maxDistance = 1000.0;
        double minTargetHealth = 0.0;
        double chance = 1.0;

        bool operator==(const Choice &) const = default;
    };
    /*! The first choice that holds is shot, else \c {projectile}. */
    QList<Choice> choices;
    /*! Most entries of \c {choices}. */
    static constexpr int maxChoices = 8;
    /*! Seconds it draws before a shot (\c {drawSeconds}), and waits after one before it draws
        again (\c {cooldownSeconds}). \qcshared */
    double drawSeconds = 1.0;
    double cooldownSeconds = 1.0;
    /*! Launch speed in m/s (the projectile's damage grows with it). */
    double speed = 25.0;
    /*! Random deviation of a shot from its aim, in degrees (the spread of the reference's
        archers on normal difficulty; this game has no difficulty). */
    double spreadDegrees = 1.5;
    /*! It shoots at targets up to \c {range} metres away; it backs off from one closer than
        \c {minDistance} and walks towards one farther than \c {keepDistance} (or one it does not
        see), circling sideways in between. All three are horizontal distances. \qcshared */
    double range = 15.0;
    double minDistance = 5.0;
    double keepDistance = 12.0;
    /*! Played where it starts to draw, \c {drawSeconds} before the shot (\c {warnSound}; the
        wailer's cry, decision E-1053); empty for none. Only drifters play it. */
    QString warnSound;
    /*! Shots of one charge (\c {volley}, 1..8; the ember wraith's three, decision E-1056), the
        later ones \c {volleySeconds} after the one before; \c {cooldownSeconds} counts from the
        last. Only drifters fire volleys. \qcshared */
    int volley = 1;
    double volleySeconds = 0.3;
    /*! Above 0 the aim sideways is off by a normal draw of this many metres per square root of
        the distance, instead of \c {spreadDegrees} (\c {scatter}; only drifters). */
    double scatter = 0.0;
    /*! It shows flames on its body from the start of a charge to the last shot of the volley
        (\c {flares}; only drifters). */
    bool flares = false;

    bool operator==(const QcArcherDefinition &) const = default;
};

/*! \brief A parent that splits off children and takes them with it (the Kjut, decision E-839), from
    its data (\c {family}): every fresh hurt it survives that takes health splits off
    \c {childrenPerHit} creatures of kind \c {child} while fewer than \c {maxChildren} of its
    children live and it has split fewer than \c {maxSplits} children in its life; when it dies,
    every child of it vanishes with \c {popSound} and leaves \c {popDrops} (like the children of a
    QObject on deleteLater), without an explosion.

    A child killed on its own leaves only its kind's drops. Children without a target of their
    own orbit the parent (\c {orbitRadius}, \c {orbitSeconds}). The client draws a thread of
    \c {tetherTexture} from each child to the nearest parent of the kind within \c {tetherRange}
    metres; the rest of the family is server side only. \qcfields */
struct QcFamilyDefinition
{
    QString child;
    /*! The kind \c {child} names among the creatures, resolved when the content is finished
        (QcMobTraitsContent::resolve); none before. */
    std::optional<QcCreatureKindId> childType;
    /*! Most of its children alive at once. */
    int maxChildren = 4;
    /*! Children split off by one hurt. */
    int childrenPerHit = 1;
    /*! Children a parent splits off in its whole life (not saved); the data's 0 or a missing
        field means twice \c {maxChildren} (QcMobReader). */
    int maxSplits = 8;
    /*! What each child leaves when it vanishes with its parent. */
    QList<QcItemDrop> popDrops;
    /*! Played where a child vanishes with its parent; empty for none. */
    QString popSound;
    /*! The middle radius of the orbits in metres (each child keeps its own, within ±30 %) and
        the seconds of one round. \qcshared */
    double orbitRadius = 1.2;
    double orbitSeconds = 4.0;
    /*! The thread the client draws between parent and child; empty texture for none. */
    QString tetherTexture;
    /*! Metres within which the client looks for the nearest parent to draw the thread to. */
    double tetherRange = 12.0;

    /*! Highest \c {maxChildren} and \c {childrenPerHit} the data may set. */
    static constexpr int maxChildrenLimit = 16;
};

/*! \brief A creature that drifts without paths (decision E-1053; the wailer, the lava wader),
    from its data (\c {drift}; the drifter AI, with \c {hover}): it holds a heading for about
    \c {seconds} and turns at a wall. With \c {keepsToLava} it picks headings that lead over lava
    where it can, with \c {keepsToGround} it stops at a drop of more than dropReach blocks and
    takes another heading (decision E-1056); with \c {restSeconds} it stays about that long
    after each heading. \qcfields */
struct QcDriftDefinition
{
    /*! The deepest drop a creature that keeps to the ground walks off. */
    static constexpr int dropReach = 3;

    double seconds = 4.0;
    bool keepsToLava = false;
    bool keepsToGround = false;
    double restSeconds = 0.0;

    bool operator==(const QcDriftDefinition &) const = default;
};

/*! \brief A creature a player rides (decision E-1054; the lava wader), from its data (\c {ride}):
    the item \c {saddle} used on it saddles it, using it saddled mounts it, and while the rider
    holds \c {steer} it goes where he looks at \c {speed} m/s. \c {seat} is the height of the
    rider's feet above its feet. \qcfields */
struct QcRideDefinition
{
    QString saddle;
    QString steer;
    QcItemId saddleId = qcNoItem;
    QcItemId steerId = qcNoItem;
    double speed = 3.0;
    double seat = 1.2;

    bool operator==(const QcRideDefinition &) const = default;
};

/*! \brief A creature that falls apart when it dies (decision E-1055; the magma slime), from its
    data (\c {splits}): \c {min} to \c {max} creatures of the kind \c {into} appear where it died.
    \qcfields */
struct QcSplitDefinition
{
    QString into;
    int min = 2;
    int max = 4;

    bool operator==(const QcSplitDefinition &) const = default;
};

/*! \brief A creature that hovers (the Kjut, decision E-839), from its data (\c {hover}): it floats
    \c {height} metres above the ground or the liquid below it, bobbing by \c {bob} metres every
    \c {bobSeconds}, never higher than \c {maxHeight} above the ground and lower under a low
    ceiling; it flies without gravity towards where its brain wants to go, rises over a wall in its
    way and turns to face where it goes at no more than \c {turnSpeed} degrees a second, rising and
    sinking at no more than \c {climbSpeed} metres a second (the wailer's slow drift, decision
    E-1053). With \c {sinksInWater} only lava carries it, not water (the lava wader, decision
    E-1054). With \c {risesToTarget} a drifter with a target stops and keeps its eyes about level
    with the target's instead of its hover height (the ember wraith, decision E-1056).

    Server side only. \qcfields */
struct QcHoverDefinition
{
    /*! The largest \c {height} and \c {maxHeight}. */
    static constexpr double highest = 24.0;

    double height = 0.6;
    double bob = 0.08;
    double bobSeconds = 2.5;
    double maxHeight = 3.0;
    double turnSpeed = 120.0;
    double climbSpeed = 3.0;
    bool sinksInWater = false;
    bool risesToTarget = false;

    bool operator==(const QcHoverDefinition &) const = default;
};
