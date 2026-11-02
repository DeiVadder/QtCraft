// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/player/QcPrediction.h"
#include "common/protocol/QcMessages.h"

#include <QString>

#include <variant>

/*! \brief What the session's tick core (QcSessionCore) tells the session, in one ordered stream
    (decision E-560): the server's messages as they arrived and, between them, the predicted ticks
    and the reconciliations of the local player.

    The session applies them in this order, so its state is the same as if it had received and
    ticked itself, only later while the GUI thread is busy. Plain values; they cross threads
    through a QcMailbox. */
struct QcSessionEvent
{
    /*! \brief A message of the server (every one but PlayerState, which comes as Reconciled). */
    struct Received
    {
        /*! The message. */
        QcMessage message;
    };

    /*! \brief The prediction as the core holds it now, and how far the core is with the
        session's commands. */
    struct Prediction
    {
        /*! The predicted player. */
        QcPrediction prediction;
        /*! The wish to fly that goes with the next input (QcClientGameMode::flightWanted). */
        bool flightWanted = false;
        /*! Mirrored commands (QcSessionCommand::isMirrored) the core has applied; the session's
            own wish to fly and reset of the corrections win while later ones are on their way. */
        quint64 mirroredApplied = 0;
    };

    /*! \brief One tick ran on the core: the player was predicted (unless dead) and the input
        sent. */
    struct Ticked
    {
        /*! The prediction after the tick. */
        Prediction after;
        /*! False while the player is dead: nothing was predicted or sent. */
        bool predicted = false;
    };

    /*! \brief The server's state of the player was applied to the prediction. */
    struct Reconciled
    {
        /*! The server's state of the player. */
        QcMsg::PlayerState state;
        /*! The prediction after it was applied. */
        Prediction after;
    };

    /*! \brief The connection closed; \c {error} is the protocol error or the transport's reason
        (may be empty). */
    struct Closed
    {
        /*! Why it closed; may be empty. */
        QString error;
    };

    /*! The event. */
    std::variant<Received, Ticked, Reconciled, Closed> what;
};
