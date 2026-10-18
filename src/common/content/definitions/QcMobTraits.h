#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QList>
#include <QString>
#include <QStringList>

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
