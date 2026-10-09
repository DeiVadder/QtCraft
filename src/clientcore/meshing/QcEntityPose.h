#pragma once

// What the animator needs to know about an entity in one frame. Movement states come from the
// server; progress values are advanced by the client.
struct QcEntityPose
{
    // Horizontal speed in m/s and the distance walked so far (drives the stride).
    double speed = 0.0;
    double walkDistance = 0.0;
    bool onGround = true;
    bool inWater = false;
    bool sneaking = false;
    bool sprinting = false;
    // 0..1 while the right arm swings (mining, attacking, using), negative otherwise.
    double swingProgress = -1.0;
    // 0..1 while the entity recoils from a hit, negative otherwise.
    double hurtProgress = -1.0;
    // 0..1 while the entity falls over dying, negative otherwise.
    double deathProgress = -1.0;
    // View pitch in degrees, positive looks up.
    float headPitch = 0.0f;
    bool armsForward = false;
};
