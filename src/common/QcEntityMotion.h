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

    bool operator==(const QcEntityMotion &) const = default;
};
