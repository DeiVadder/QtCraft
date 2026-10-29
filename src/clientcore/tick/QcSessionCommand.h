// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPlayerInput.h"
#include "common/protocol/QcMessages.h"

#include <variant>

// What the session asks of its tick core (QcSessionCore), in the order it asked (decision E-560).
// Plain values; they cross threads through a QcMailbox.
struct QcSessionCommand
{
    // Sends Hello and starts ticking (TickMode::Timer) or waits for tick() (TickMode::Manual).
    struct Start
    {
        QcMsg::Hello hello;
        bool timer = true;
    };

    // The movement intent for the next ticks (already cleaned, QcPrediction::setIntent).
    struct SetIntent
    {
        QcPlayerInput intent;
    };

    // The player's wish to fly (QcClientGameMode::setFlightWanted).
    struct SetFlight
    {
        bool wanted = false;
    };

    // Forget the largest correction so far (QcPrediction::resetCorrections).
    struct ResetCorrections
    {
    };

    // A message to the server.
    struct Send
    {
        QcMessage message;
    };

    // The blocks the local player's action numbered `sequence` is expected to set, shown in the
    // core's block mirror too (QcBlockMirror::predict, decision E-853); sent before the action.
    struct PredictBlocks
    {
        quint32 sequence = 0;
        QList<QcMsg::BlockUpdate> blocks;
    };

    // Close the connection (after the messages sent before).
    struct Close
    {
    };

    std::variant<Start, SetIntent, SetFlight, ResetCorrections, Send, PredictBlocks, Close> what;

    // Whether the session takes the command's effect back from the core's prediction (the wish to
    // fly, the reset of the corrections) and so must wait until the core applied it
    // (QcSessionEvent::Prediction::mirroredApplied).
    bool isMirrored() const { return std::holds_alternative<SetFlight>(what) || std::holds_alternative<ResetCorrections>(what); }
};
