// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtGlobal>

#include <cmath>

/*! \brief What the status effects of a player do to his steps (QcEffectRules::movement, decision
    E-992): QcEntityMotion::stepPlayer steps with them, alike on the client and on the server.
    \qcfields */
struct QcMovementEffects
{
    /*! Factor on his speed (speed, slowness). */
    float speed = 1.0f;
    /*! Factor on the velocity of his jump (jump boost). */
    float jump = 1.0f;
    /*! The fastest he falls in m/s (slow falling), 0 for no limit. */
    float maxFallSpeed = 0.0f;

    bool operator==(const QcMovementEffects &) const = default;
};

/*! \brief Movement intent for one simulation tick, sent from client to server. */
struct QcPlayerInput
{
    /*! Numbers the inputs of a connection; the server acknowledges them (PlayerState). */
    quint32 sequence = 0;
    /*! Each in [-1, 1]; forward is along the view direction projected onto the ground. \qcshared */
    float forward = 0.0f;
    float strafe = 0.0f;
    /*! The player wants to jump (swim up, climb, fly up). */
    bool jump = false;
    /*! The player wants to sneak (fly down). */
    bool sneak = false;
    /*! The player wants to sprint. */
    bool sprint = false;
    /*! The player wants to fly (toggled by a double tap of jump); only granted where the game
        mode allows it, and dropped by the physics on landing. */
    bool flying = false;
    /*! A spectator's flight speed in tenths of the normal one (minFlySpeed to maxFlySpeed,
        normalFlySpeed by default), set with the mouse wheel (decision E-730); other modes ignore
        it. \qcshared */
    static constexpr quint8 minFlySpeed = 1;
    static constexpr quint8 normalFlySpeed = 10;
    static constexpr quint8 maxFlySpeed = 40;
    quint8 flySpeed = normalFlySpeed;
    /*! The view direction in degrees, see QcCamera for the convention. \qcshared */
    float yaw = 0.0f;
    float pitch = 0.0f;
    /*! The player holds an item in use (draws a bow) in this input (decision E-986): he moves at
        QcPhysics::usingItemFactor of his speed and does not sprint. Sent, and the server steps the
        input with it even when it knows of no draw, so that client and server agree on every
        input; it also slows a player it knows to draw when this is not set. */
    bool usingItem = false;
    /*! The count of the changes of what status effects do to the player's moving
        (QcMsg::PlayerHealth::movementEpoch) that this input was predicted with (decision E-992).
        Sent, and the server steps the input with the effects of that count (QcMovementEpochs), so
        an effect changes the move of the same input on both sides, however late the client hears
        of it. */
    quint16 movementEpoch = 0;
    /*! What the player's status effects do to this step, set by the side that steps the input:
        the client from those the server told it, the server by \c {movementEpoch}. Never sent. */
    QcMovementEffects effects = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    /*! The push of the creatures the player overlaps (decision E-882), a horizontal velocity in
        m/s added to his move, set like \c {effects} by the side that steps the input: the server
        from its creatures, the client from those it knows (QcPushBodies). Never sent. */
    QcVec3 push = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

    /*! Returns whether this input sprints: the wish to, moving forward and no item in use
        (decision E-986). Sneaking is not looked at: a walker's sneak goes first in
        QcEntityMotion::stepPlayer, the server checks it for the figure, and a flyer's sneak only
        descends. */
    [[nodiscard]] bool sprints() const { return sprint && forward > 0.0f && !usingItem; }

    bool operator==(const QcPlayerInput &) const = default;
};

/*! Returns \a degrees wrapped into [0, 360), non-finite values give 0. Adding 360 to a tiny
    negative remainder of fmod rounds to exactly 360 in float; that case maps to 0, so the result
    is always below 360. */
[[nodiscard]] inline float qcWrappedYaw(float degrees)
{
    if (!std::isfinite(degrees)) {
        return 0.0f;
    }

    const float remainder = std::fmod(degrees, 360.0f);
    const float positive = remainder < 0.0f ? remainder + 360.0f : remainder;
    return positive >= 360.0f ? 0.0f : positive;
}
