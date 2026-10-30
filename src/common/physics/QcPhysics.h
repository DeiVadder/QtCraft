// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"
#include "common/world/QcBlockAccess.h"

/*! \brief Deterministic movement and collision shared by server (authoritative) and client
    (prediction).

    Units: metres, seconds, metres per second. */
namespace QcPhysics {

/*! Ticks per second of the simulation. */
inline constexpr double tickRate = 20.0;
/*! Seconds per tick. */
inline constexpr double tickDuration = 1.0 / tickRate;

/*! Width of the player's collision box. */
inline constexpr double playerWidth = 0.6;
/*! Height of the player's collision box. */
inline constexpr double playerHeight = 1.8;
/*! Height of the player's eyes above the feet. */
inline constexpr double playerEyeHeight = 1.62;

/*! Downward acceleration out of liquids (m/s²). */
inline constexpr double gravity = 28.0;
/*! Fastest fall out of liquids. */
inline constexpr double terminalVelocity = 60.0;
/*! Upward velocity of a jump from the ground (scaled by SurfaceFactors::jump). */
inline constexpr double jumpVelocity = 9.0;
/*! Target speeds of a player walking, sprinting and sneaking. \qcshared */
inline constexpr double walkSpeed = 4.3;
inline constexpr double sprintSpeed = 5.6;
inline constexpr double sneakSpeed = 1.3;
/*! How fast the horizontal velocity approaches the wished one, per second (the share
    acceleration x dt per tick), on the ground and in the air. \qcshared */
inline constexpr double groundAcceleration = 20.0;
inline constexpr double airAcceleration = 4.0;

/*! Target speed of a player in a liquid. */
inline constexpr double swimSpeed = 2.2;
/*! How fast the horizontal velocity approaches the wished one in a liquid, per second. */
inline constexpr double waterAcceleration = 8.0;
/*! Downward acceleration in a liquid (m/s²). */
inline constexpr double waterGravity = 6.0;
/*! Fastest sinking in a liquid (scaled by the liquid's factor). */
inline constexpr double waterSinkSpeed = 2.0;
/*! Fastest rise when swimming up (scaled by the liquid's factor), and the acceleration towards it
    (m/s²). \qcshared */
inline constexpr double swimUpSpeed = 3.0;
inline constexpr double swimUpAcceleration = 18.0;
/*! Leaving a liquid at its shore (decision E-918): a player whose feet are still in the liquid
    but whose body is above it (QcEntityMotion::inWater false) and who walks into a wall is
    lifted with this speed, enough to step out onto a bank one block above the liquid's cell, not
    onto a higher one. The same rule in water and lava, on server and client. */
inline constexpr double liquidExitSpeed = 7.0;

/*! Creative flight (decision E-284): horizontal speed, with sprint, and the speed up and down
    with jump and sneak; the velocity eases towards them like on the ground, without gravity, by
    the share flyAcceleration x dt per tick. \qcshared */
inline constexpr double flySpeed = 10.9;
inline constexpr double flySprintSpeed = 21.6;
inline constexpr double flyVerticalSpeed = 7.5;
inline constexpr double flyAcceleration = 10.0;

/*! Bodies on the ground walk up steps this high without jumping (slabs, stairs). */
inline constexpr double stepHeight = 0.5;

/*! On ladders: speed upwards while pushing against the wall or jumping, and the fastest slide
    down. \qcshared */
inline constexpr double climbSpeed = 2.4;
inline constexpr double climbDownSpeed = 2.0;

/*! \brief The outcome of moveBox(): the movement that was possible and the axes on which blocks
    stopped it. \qcfields */
struct MoveResult
{
    QcVec3 applied;
    bool blockedX = false;
    bool blockedY = false;
    bool blockedZ = false;
};

/*! Returns the box of a standing body of \a width and \a height with its feet at the middle of its
    bottom face, \a feet. */
QcAabb boxAt(const QcVec3 &feet, double width, double height);
/*! A body lying on its back (a sleeping villager, decision E-411) turns around this height above
    its feet: its feet end up this far ahead of \c {feet} in the direction it faces, its head
    behind. */
inline constexpr double lyingPivot = 0.15;
/*! Returns the box of a body of \a width and \a height lying on its back around \a feet and
    facing \a yaw (along an axis, as beds lie): along its length from lyingPivot ahead of \a feet
    to \a height - lyingPivot behind them, \a width across, half its width high. */
QcAabb lyingBoxAt(const QcVec3 &feet, float yaw, double width, double height);

/*! Returns \c true when the block at \a pos in \a world blocks movement. Unloaded chunks inside
    the world's height are solid so nothing falls through missing data; the void below the world
    is open (decision E-288). */
bool isSolidAt(const QcBlockAccess &world, QcBlockPos pos);
/*! Returns the world-space collision boxes of the block at \a pos in \a world (a unit box where
    data is missing, none below the world). */
QList<QcAabb> collisionBoxesAt(const QcBlockAccess &world, QcBlockPos pos);
/*! Returns whether \a box overlaps the collision boxes of any block in \a world (a unit box
    where data is missing): a body there is stuck inside blocks. */
[[nodiscard]] bool overlapsBlocks(const QcBlockAccess &world, const QcAabb &box);
/*! Returns whether \a box overlaps a climbable block in \a world. */
bool touchesClimbable(const QcBlockAccess &world, const QcAabb &box);
/*! How far around its box a wall climber (a spider, decision E-451) feels for a wall, sideways
    (a body of 0.6 m in the middle of a cell beside a wall reaches it, as the paths assume), and
    how far above the first metre of its body (so a small body reaches the face of an overhang).
    \qcshared */
inline constexpr double clingReach = 0.25;
inline constexpr double clingAbove = 0.1;
/*! Returns whether a wall climber with the box \a box touches a block of \a world beside it or
    above it: from a hair above its feet up to clingAbove over its top or its first metre,
    whichever is higher, grown sideways by clingReach. */
bool clingsToWall(const QcBlockAccess &world, const QcAabb &box);
/*! Returns whether the block at \a pos in \a world is a liquid. */
bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos);
/*! Returns whether \a pos in \a world holds water: a liquid that sets nothing alight (not
    lava). */
bool isWaterAt(const QcBlockAccess &world, QcBlockPos pos);
/*! Returns whether the feet at \a feet stand in a liquid of \a world (the cell a hand's width
    above them), also while the body is above the surface (QcEntityMotion::inWater false): such a
    body climbs out at a bank and lands softly (decision E-918). */
[[nodiscard]] bool feetInLiquid(const QcBlockAccess &world, const QcVec3 &feet);
/*! Returns the movement factor of the liquid at \a pos in \a world (lava is slow), 1 outside
    liquids. */
double liquidFactorAt(const QcBlockAccess &world, QcBlockPos pos);
/*! Returns the smallest movement factor of the blocks of \a world that \a box overlaps
    (cobwebs), 1 without any. */
double slowdownInside(const QcBlockAccess &world, const QcAabb &box);

/*! Slippery ground (decision E-430): on a block whose slipperiness (QcBlockDefinition, units of
    the original game) lies above \c {grippingSlipperiness}, a body on the ground keeps
    slipperiness x drag of its horizontal velocity per tick (the original game's rule) instead of
    taking its wished velocity at once. \c {footDrag} is the drag of walkers (ice 0.98 keeps 89 %
    per tick), \c {itemDrag} that of dropped items (96 %). A body that coasts (\c {coastDrag}: a
    minecart off the rails) keeps the whole slipperiness on any ground: 60 % per tick where the
    ground grips (it stops within a few ticks), 98 % on ice. The value lives with the block data
    (QcBlockDefinition::grippingSlipperiness). \qcshared */
inline constexpr double grippingSlipperiness = QcBlockDefinition::grippingSlipperiness;
inline constexpr double footDrag = 0.91;
inline constexpr double itemDrag = 0.98;
inline constexpr double coastDrag = 1.0;

/*! \brief Walking speed and jump factors of the block under a body's feet (mud), and its
    slipperiness (ice). */
struct SurfaceFactors
{
    /*! Factor of the walking speed (QcBlockDefinition::surfaceSpeed). */
    double speed = 1.0;
    /*! Factor of the jump velocity (QcBlockDefinition::surfaceJump). */
    double jump = 1.0;
    /*! Slipperiness of the block (QcBlockDefinition::slipperiness). */
    double slipperiness = grippingSlipperiness;
};
/*! Returns the factors of the block just below \a feet in \a world; the defaults where there is
    no valid block. */
SurfaceFactors surfaceUnder(const QcBlockAccess &world, const QcVec3 &feet);
/*! Returns the block cell that holds \a point (each coordinate rounded down). */
QcBlockPos blockContaining(const QcVec3 &point);

/*! Returns the move of \a box by \a delta, clipped against the solid blocks of \a world (Y first,
    then X, then Z). */
MoveResult moveBox(const QcAabb &box, const QcVec3 &delta, const QcBlockAccess &world);

/*! \brief What an entity wants for one tick: \c {wish} is the horizontal movement direction (length
    <= 1), \c {speed} its target speed, \c {jump} jumps on ground, swims up in water or climbs,
    \c {hold} keeps a climber in place, \c {slideDrag} is how it slides on slippery ground
    (footDrag, itemDrag, coastDrag).

    A body that \c {climbsWalls} (spiders) holds on to any wall it touches (clingsToWall) as to a
    ladder. \c {push} is a horizontal velocity added to this tick's move only, not kept in the
    body's velocity (the push of the bodies it overlaps, decision E-882): it stops with the
    overlap, also on ice. */
struct Steering
{
    /*! Horizontal movement direction (length <= 1). */
    QcVec3 wish;
    /*! Target speed. */
    double speed = 0.0;
    /*! Jumps on the ground, swims up in a liquid, climbs. */
    bool jump = false;
    /*! Keeps a climber in place. */
    bool hold = false;
    /*! How the body slides on slippery ground (footDrag, itemDrag, coastDrag). */
    double slideDrag = footDrag;
    /*! Holds on to any wall it touches as to a ladder (spiders). */
    bool climbsWalls = false;
    /*! Horizontal velocity added to this tick's move only (see the struct). */
    QcVec3 push = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
};

/*! Returns the share of its horizontal velocity a body on ground of \a slipperiness keeps per
    tick of \a dt seconds: (slipperiness x \a drag) per 1/20 s on slippery ground or for a body
    that coasts, else 0. */
double slideKeep(double slipperiness, double drag, double dt);

/*! \brief Width and height of an entity's collision box. */
struct BodySize
{
    /*! Width of the box. */
    double width = playerWidth;
    /*! Height of the box. */
    double height = playerHeight;
};

// The steps of a body are methods of QcEntityMotion (step, stepPlayer).

} // namespace QcPhysics
