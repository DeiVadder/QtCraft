// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include <memory>

class QTcpSocket;
class QTimer;

// Ends a TCP connection so that the peer reads everything sent before (decision E-904). Closing at
// once with unread or later arriving data makes the system reset the connection, and a Windows
// peer throws away what it had not read yet: the reason of a Disconnect. So the goodbye
// - waits until Qt's buffers are empty, then shuts the socket's write side down (FIN) while its
//   read side stays open; over TLS only when the peer left first (its Disconnect arrived), else the
//   peer is expected to close on the Disconnect it reads, and the end waits for that;
// - waits until the system has sent everything (where it tells), then gives the peer the linger
//   time to close too, reading and throwing away whatever it still sends;
// - closes the socket then, at the latest after four linger times (a peer that reads nothing).
// Lives as a child of its socket, in the socket's thread.
class QcTcpGoodbye : public QObject
{
    Q_OBJECT

public:
    // Starts the goodbye of a connected `socket`, once; the owner keeps the socket. Closes a socket
    // that is not connected at once. `peerLeft`: the peer said goodbye first.
    static void begin(QTcpSocket &socket, int lingerMs, bool peerLeft);
    // Takes `owned` over and starts its goodbye: the socket deletes itself when the goodbye ended or
    // when its thread ends.
    static void release(std::unique_ptr<QTcpSocket> owned, int lingerMs, bool peerLeft);
    // Runs the calling thread's event loop until the sockets released in it ended their goodbye, at
    // most `maxWaitMs`, and ends the rest; for a thread that is about to end, which would otherwise
    // cut them off at once. Returns at once in a thread without.
    static void finishLingering(int maxWaitMs = 1'000);

private:
    QcTcpGoodbye(QTcpSocket &socket, int lingerMs, bool peerLeft);

    // Moves the goodbye on: the end once Qt's buffers are empty, the linger time once the system
    // sent everything.
    void step();
    // Throws away what the peer sent.
    void discardReceived();
    // Closes the socket (the peer closed or the time ran out).
    void finish();
    bool hasFinished() const { return m_finished; }
    // The socket's thread ended: the socket goes without its goodbye.
    void onThreadFinished();

    QTcpSocket &m_socket;
    // Checks the system's send queue until it is empty.
    QTimer *m_poll = nullptr;
    QTimer *m_linger = nullptr;
    QTimer *m_limit = nullptr;
    int m_lingerMs = 0;
    // Whether the end (FIN) goes out as soon as Qt's buffers are empty.
    bool m_endEarly = false;
    bool m_endSent = false;
    bool m_finished = false;
    bool m_released = false;
};
