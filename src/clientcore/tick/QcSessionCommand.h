// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPlayerInput.h"
#include "common/protocol/QcMessages.h"

#include <variant>

/*! \brief What the session asks of its tick core (QcSessionCore), in the order it asked (decision
    E-560).

    Plain values; they cross threads through a QcMailbox. */
struct QcSessionCommand
{
    /*! \brief Sends Hello and starts ticking (TickMode::Timer) or waits for tick()
        (TickMode::Manual). */
    struct Start
    {
        /*! The Hello to send. */
        QcMsg::Hello hello;
        /*! Whether a timer ticks (else tick() by hand). */
        bool timer = true;
    };

    /*! \brief The movement intent for the next ticks (already cleaned, QcPrediction::setIntent). */
    struct SetIntent
    {
        /*! The intent. */
        QcPlayerInput intent;
    };

    /*! \brief The player's wish to fly (QcClientGameMode::setFlightWanted). */
    struct SetFlight
    {
        /*! Whether the player wants to fly. */
        bool wanted = false;
    };

    /*! \brief Forget the largest correction so far (QcPrediction::resetCorrections). */
    struct ResetCorrections
    {
    };

    /*! \brief A message to the server. */
    struct Send
    {
        /*! The message. */
        QcMessage message;
    };

    /*! \brief The blocks the local player's action numbered \c {sequence} is expected to set,
        shown in the core's block mirror too (QcBlockMirror::predict, decision E-853); sent
        before the action. */
    struct PredictBlocks
    {
        /*! The action's number. */
        quint32 sequence = 0;
        /*! The blocks it is expected to set. */
        QList<QcMsg::BlockUpdate> blocks;
    };

    /*! \brief Close the connection (after the messages sent before). */
    struct Close
    {
    };

    /*! The command. */
    std::variant<Start, SetIntent, SetFlight, ResetCorrections, Send, PredictBlocks, Close> what;

    /*! Returns whether the session takes the command's effect back from the core's prediction
        (the wish to fly, the reset of the corrections) and so must wait until the core applied
        it (QcSessionEvent::Prediction::mirroredApplied). */
    bool isMirrored() const { return std::holds_alternative<SetFlight>(what) || std::holds_alternative<ResetCorrections>(what); }
};
