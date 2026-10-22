#pragma once

#include <QtGlobal>

// Movement intent for one simulation tick, sent from client to server.
struct QcPlayerInput
{
    quint32 sequence = 0;
    // Each in [-1, 1]; forward is along the view direction projected onto the ground.
    float forward = 0.0f;
    float strafe = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    // The player wants to fly (toggled by a double tap of jump); only granted where the game mode
    // allows it, and dropped by the physics on landing.
    bool flying = false;
    // A spectator's flight speed in tenths of the normal one, set with the mouse wheel (decision
    // E-730); other modes ignore it.
    static constexpr quint8 minFlySpeed = 1;
    static constexpr quint8 normalFlySpeed = 10;
    static constexpr quint8 maxFlySpeed = 40;
    quint8 flySpeed = normalFlySpeed;
    // Degrees, see QcCamera for the convention.
    float yaw = 0.0f;
    float pitch = 0.0f;
    // The factor of the status effects on the player's speed (slowness, decision E-838), set by the
    // side that steps the input: the server from the player's effects, the client from those the
    // server told it. Never sent.
    float speedFactor = 1.0f;

    bool operator==(const QcPlayerInput &) const = default;
};
