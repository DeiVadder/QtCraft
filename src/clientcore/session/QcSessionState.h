// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtCore/qobjectdefs.h>

/*! \brief The phases of a client session's connection; the phase itself is QcSessionState
    (QcClientSession::State, reported by QcClientSession::stateChanged()). */
namespace QcSessionStates {
Q_NAMESPACE

/*! \brief The phase of a client session's connection. */
enum class State {
    /*! Hello sent, waiting for the server's Welcome. */
    Connecting,
    /*! Welcomed, waiting for the world and the first state of the player. */
    Loading,
    /*! In the world. */
    Playing,
    /*! The connection ended (QcClientSession::disconnectReason()). */
    Disconnected,
};
Q_ENUM_NS(State)

/*! \brief Why a client session's connection ended (decision E-964). */
enum class Ending {
    /*! Not ended. */
    None,
    /*! The server closed the connection without a word. */
    PeerClosed,
    /*! The connection broke (a socket error; the reason is its text). */
    Lost,
    /*! The server sent a Disconnect (QcClientSession::disconnectMessage()). */
    Refused,
    /*! The server speaks another protocol version. */
    ProtocolMismatch,
    /*! This client ended the connection over an error of its own (a message it cannot take). */
    LocalError,
};
Q_ENUM_NS(Ending)

} // namespace QcSessionStates

/*! The phase of a client session's connection (QcSessionStates::State), the name the code uses. */
using QcSessionState = QcSessionStates::State;
/*! Why a client session's connection ended (QcSessionStates::Ending). */
using QcSessionEnding = QcSessionStates::Ending;
