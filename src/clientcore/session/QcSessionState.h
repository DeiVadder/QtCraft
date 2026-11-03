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

} // namespace QcSessionStates

/*! The phase of a client session's connection (QcSessionStates::State), the name the code uses. */
using QcSessionState = QcSessionStates::State;
