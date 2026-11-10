// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QObject>

#include <functional>
#include <utility>

/*! \brief A screen the server opened at a block for the local player (the anvil, the enchanting
    table): whether it is open, where, and the way out to the server.

    The server opens it, sends its state and closes it; the player's requests go out only while it
    is open. QcClientBlockScreenOf adds the messages of one kind of screen. Part of
    QcClientSession; GUI-free. */
class QcClientBlockScreen : public QObject
{
    Q_OBJECT

public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;

    /*! Sets the way out to the server to \a sender (the session's connection). */
    void setSender(Sender sender) { m_send = std::move(sender); }
    /*! Returns whether the screen is open. */
    bool isOpen() const { return m_open; }
    /*! Returns where the screen's block is. */
    QcBlockPos pos() const { return m_pos; }
    /*! Returns the way out to the server (empty before setSender()). */
    const Sender &messageSender() const { return m_send; }

signals:
    /*! The server opened the screen. */
    void opened();
    /*! The state the server sent changed. */
    void changed();
    /*! The screen closed (by the server or by the player). */
    void closed();

protected:
    /*! Sends the player's request \a message. Returns false, sending nothing, when the screen is
        not open or there is no way out. */
    bool sendWhileOpen(const QcMessage &message) const;
    /*! The server opened the screen at \a pos. */
    void markOpened(QcBlockPos pos);
    /*! The server closed the open screen. */
    void markClosedByServer();
    /*! The player closed the open screen; tells the server with \a message. */
    void markClosedByPlayer(const QcMessage &message);

private:
    void setOpen(bool open) { m_open = open; }

    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
};

/*! \brief A screen of the kind the server opens with the message \a Open, updates with the
    message \a State and closes with the message \a Close (either side sends \a Close).

    The state is kept only while the screen is open; a state still on its way after the player's
    own close belongs to no open screen and is dropped. */
template<typename Open, typename State, typename Close>
class QcClientBlockScreenOf : public QcClientBlockScreen
{
public:
    /*! Takes over \a message when it is \c Open, \c State or \c Close. Returns false for other
        messages. */
    virtual bool handle(const QcMessage &message);
    /*! Returns the state as the server last sent it (empty while closed). */
    const State &state() const { return m_state; }
    /*! The player closed the screen; the server gives the slots back. */
    void close();

protected:
    /*! Returns whether \a a and \a b show the same on the screen (no changed() between them). */
    virtual bool sameState(const State &a, const State &b) const = 0;
    /*! Sets the state to \a state. */
    void setState(const State &state) { m_state = state; }

private:
    State m_state;
};

template<typename Open, typename State, typename Close>
bool QcClientBlockScreenOf<Open, State, Close>::handle(const QcMessage &message)
{
    if (std::holds_alternative<Close>(message)) {
        if (isOpen()) {
            setState({});
            markClosedByServer();
        }
        return true;
    }
    if (const auto *state = std::get_if<State>(&message)) {
        if (isOpen() && !sameState(*state, this->state())) {
            setState(*state);
            emit changed();
        }
        return true;
    }
    const auto *open = std::get_if<Open>(&message);
    if (!open) {
        return false;
    }
    markOpened(open->pos);
    return true;
}

template<typename Open, typename State, typename Close>
void QcClientBlockScreenOf<Open, State, Close>::close()
{
    if (!isOpen()) {
        return;
    }
    setState({});
    markClosedByPlayer(Close{});
}
