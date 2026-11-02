// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcServerDebugMirror.h"
#include "common/protocol/QcMessages.h"

#include <functional>

/*! \brief The client's subscription to the server's debug data (QcMsg::DebugSubscribe, decision
    E-225) and what arrived of it.

    Two sets of flags are asked for together: those the user or the control interface chooses,
    and base flags that stay subscribed whatever the others ask for (the statistics log of a
    headless game, which the control command \c {debug off} must not cut off). Changes are sent
    at once while the server has welcomed the client and the connection is open, before the
    welcome with the welcome, after the connection closed never. */
class QcClientDebugSubscription
{
public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;

    /*! Sets the way out to the server to \a sender (the session's connection). */
    void setSender(Sender sender) { m_send = std::move(sender); }

    /*! Sets the subscription to \a flags (QcMsg::DebugSubscribe flags, 0 for none). */
    void setFlags(quint8 flags);
    /*! Sets the base \a flags, which stay subscribed whatever setFlags() asks for. */
    void setBaseFlags(quint8 flags);
    /*! Returns what the server is asked for: both together. */
    quint8 flags() const { return m_flags | m_baseFlags; }
    /*! The server welcomed the client: what is wanted goes out now. */
    void welcomed();
    /*! The connection closed: nothing goes out any more. */
    void closed() { m_welcomed = false; }
    /*! Takes a ServerDebug \a message into the mirror. */
    void receive(const QcMsg::ServerDebug &message) { m_mirror.receive(message); }
    /*! Returns what arrived of the server's debug data. */
    const QcServerDebugMirror &mirror() const { return m_mirror; }

private:
    // Tells the server when the subscription differs from `before`.
    void sendChange(quint8 before);

    Sender m_send;
    quint8 m_flags = 0;
    quint8 m_baseFlags = 0;
    bool m_welcomed = false;
    QcServerDebugMirror m_mirror;
};
