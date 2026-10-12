#pragma once

#include "QcVec3.h"

// Kinematic state of an entity. Position is the centre of the bottom face (feet).
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
    // landing. Only players fly this way.
    bool flying = false;

    bool operator==(const QcEntityMotion &) const = default;
};
