// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/gameplay/QcGameMode.h"

class QcBlockAccess;
struct QcPlayerInput;

namespace QcPhysics {
struct BodySize;
struct MoveResult;
struct Steering;
} // namespace QcPhysics

// Kinematic state of an entity. Position is the centre of the bottom face (feet). The steps are
// the shared physics of server and client (QcPhysics.cpp).
struct QcEntityMotion
{
    QcVec3 position;
    QcVec3 velocity;
    bool onGround = false;
    bool inWater = false;
    // Inside a climbable block (ladder) during the last step.
    bool climbing = false;
    // Inside a block that slows bodies (cobweb) during the last step; breaks a fall like a ladder.
    bool slowed = false;
    // Flying in creative mode (decision E-284): no gravity, up and down with jump and sneak; ends on
    // landing. Only players fly this way; a spectator always flies (decision E-730).
    bool flying = false;

    // Advances any entity by one tick. The caller updates inWater before the call. Returns the move,
    // whose blocked axes a walking creature reads (QcMobBehaviour); other bodies ignore it.
    QcPhysics::MoveResult step(const QcPhysics::Steering &steering, QcPhysics::BodySize size, const QcBlockAccess &world, double dt);
    // Advances a player by one tick of `dt` seconds. `movement` comes from the player's game mode
    // (QcGameModes::Rules::movement): on foot a wish to fly is ignored and a flight ends; NoClip
    // flies through blocks whatever the wish.
    void stepPlayer(const QcPlayerInput &input, const QcBlockAccess &world, double dt, QcMovement movement);

    bool operator==(const QcEntityMotion &) const = default;

private:
    // The vertical velocity in air after one tick: gravity, jumping and ladders.
    [[nodiscard]] double airVelocityY(const QcPhysics::Steering &steering, QcPhysics::BodySize size, const QcBlockAccess &world,
                                      double dt) const;
    // Takes over a move: the new position, standing on ground and the velocity blocked by walls.
    void applyMove(const QcPhysics::MoveResult &move);
    // One tick of creative flight towards `wish`.
    void stepFlying(const QcPlayerInput &input, const QcVec3 &wish, const QcBlockAccess &world, double dt);
    // One tick of a spectator's flight towards `wish` (decision E-730): like creative flight, at the
    // speed of input.flySpeed, through every block.
    void stepNoClip(const QcPlayerInput &input, const QcVec3 &wish, double dt);
    // Holds the feet within [QcWorld::lowestBody, QcWorld::highestBody] (decision E-883), the
    // centres of the outermost block columns of the world, and stops the velocity across it; the
    // same rule on server and client.
    void keepInsideWorld();
};
