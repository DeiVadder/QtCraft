#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// A creature that a player provokes by looking at its head (the enderman, decision E-460), from its
// data (`gaze`): the player's look and the direction from their eyes to the middle of its head agree
// within `tolerance`, which narrows with the distance d (look · direction > 1 − tolerance / d, like the
// reference game: about 4.5° at 8 m, 1.8° at 50 m), from no farther than `range` metres, with nothing
// in between. Server side only.
struct QcGazeDefinition
{
    double tolerance = 0.025;
    double range = 64.0;
    // Where its head is, as a share of its height (the middle of the head).
    double headShare = 0.9;

    bool operator==(const QcGazeDefinition &) const = default;
};

// A creature that crumbles into blocks where it touches a source block of a liquid (the sand golem,
// decision E-593), from its data (`crumbles`): it is gone without loot or experience, and its build
// stands in its place, an iron golem's T in reverse: `body` at its feet, in the middle above them and
// on both sides of the middle across its facing, `head` on the middle. Each block goes into an open
// cell (air, a plant, flowing liquid; never a source); where none is open the block drops as an item.
// Server side only.
struct QcCrumbleDefinition
{
    QString liquid;
    QString body;
    QString head;
    // Played where it crumbles; empty for none.
    QString sound;

    bool operator==(const QcCrumbleDefinition &) const = default;
};

// A creature that teleports (the enderman, decision E-460), from its data (`teleport`): to a random
// safe place within `range` metres (a solid floor, room for its body, no liquid, only in loaded
// chunks; QcSafeSpot), at most once per `cooldownSeconds`. Server side only.
struct QcTeleportDefinition
{
    double range = 32.0;
    // Random places tried per teleport before it gives up.
    int attempts = 16;
    double cooldownSeconds = 0.5;
    // Projectiles do not hurt it; it teleports away instead (and the projectile bounces off).
    bool dodgesProjectiles = true;
    // It teleports away while it is in water or the rain falls on it.
    bool whenWet = true;
    // Chance per second that an angry one far from its target teleports towards it.
    double towardsTargetChance = 0.05;
    // Sound key played where it leaves and where it arrives; empty for silence.
    QString sound;

    bool operator==(const QcTeleportDefinition &) const = default;
};

// A creature that takes single blocks and puts them down again (the enderman, decision E-460), from
// its data (`carries`): only the blocks of `blocks`, and only while the game rule mobGriefing is on.
// Server side only.
struct QcCarryDefinition
{
    // Names of the blocks it takes (any state of them).
    QStringList blocks;
    // Their ids (QcBlockId of the default state), resolved when the content is finished; names
    // without a block or an item of their own are left out with a warning.
    QList<QcBlockId> blockIds;
    // Chance per second to take a block while it carries none, and to put one down while it does.
    double takeChance = 0.05;
    double placeChance = 0.0015;
    // How far around its feet it looks for a block (horizontally; from the floor up to `reach` + 1
    // above its feet).
    int reach = 2;

    static constexpr int maxBlocks = 64;

    bool operator==(const QcCarryDefinition &) const = default;
};

// A creature that players tame (wolves, cats; decision E-480), from its data (`tameable`): each
// of its taming `items` used on it tames it with `chance` (a tame one belongs to that player's
// identity, follows, sits when its owner uses it and fights for its owner with `defends`); a wolf
// `becomes` a dog. Server side only, except that the client knows a kind is tameable at all
// (it sends every use of one to the server).
struct QcTameDefinition
{
    // Names of the items that tame it; empty for a kind that is only ever tame (the dog).
    QStringList items;
    // Chance that one item tames it.
    double chance = 1.0 / 3.0;
    // The kind it turns into when tamed (a wolf into a dog); empty: it stays its kind.
    QString becomes;
    // The index of `becomes` in the content's mobs, resolved when the content is finished; -1 for none.
    int becomesType = -1;
    // Food that heals a tame one by `healAmount` (meat for dogs).
    QStringList healItems;
    double healAmount = 4.0;
    // A tame one that does not sit follows its owner when farther than `followDistance` metres, until
    // it is within `stopDistance`, and teleports next to its owner when farther than
    // `teleportDistance` (only to a safe place in loaded chunks; QcSafeSpot).
    double followDistance = 6.0;
    double stopDistance = 2.0;
    double teleportDistance = 12.0;
    // It attacks what hurts its owner and what its owner strikes (never its owner, never another pet
    // of the same owner).
    bool defends = false;
    // It wears a collar whose colour a dye of `collarDyes` (dye name to RGB) changes; it starts with
    // the colour of `collarDefault` (a key of collarDyes). Without collarDyes it wears no collar.
    QStringList collarDyes;
    QList<quint32> collarColors;
    QString collarDefault;

    static constexpr int maxItems = 32;

    // The colour of the dye `name`, if the collar takes it.
    std::optional<quint32> collarColor(const QString &name) const
    {
        const qsizetype index = collarDyes.indexOf(name);
        return index < 0 ? std::nullopt : std::optional<quint32>(collarColors[index]);
    }
    bool hasCollar() const { return !collarDyes.isEmpty(); }

    bool operator==(const QcTameDefinition &) const = default;
};

// A shy creature (the ocelot, decision E-480), from its data (`shy`): it flees from players closer
// than `distance` metres, except from one who holds one of its `calmItems` and moves no faster than
// `calmSpeed` m/s (sneaking up slowly with its food). Server side only.
struct QcShyDefinition
{
    double distance = 8.0;
    QStringList calmItems;
    double calmSpeed = 1.5;

    bool operator==(const QcShyDefinition &) const = default;
};

// A creature that lives in water (fish, decision E-502), from its data (`aquatic`): it swims only in
// water, in schools with its kind (cohesion, alignment and separation among at most `neighbours`
// creatures of its kind within `schoolRadius` metres), flops about on land and suffocates there
// after `airSeconds`. Water creatures have their own cap (QcServerLimits::waterCreaturesPerPlayer)
// and never count as animals. Server side only.
struct QcAquaticDefinition
{
    // Speed in m/s while swimming; it darts away at the creature's chaseSpeed when hurt.
    double swimSpeed = 1.5;
    // Seconds it lives out of water; after them it takes `suffocationDamage` every second.
    double airSeconds = 15.0;
    double suffocationDamage = 1.0;
    // Upward speed in m/s of a flop on land (sideways a third of it).
    double flopSpeed = 4.0;
    // Schooling, as weights of the steering it adds to its wandering.
    double schoolRadius = 4.0;
    double cohesion = 1.5;
    double alignment = 0.8;
    double separation = 1.2;
    double separationDistance = 0.8;
    int neighbours = 8;

    bool operator==(const QcAquaticDefinition &) const = default;
};
