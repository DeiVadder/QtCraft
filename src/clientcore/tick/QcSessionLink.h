#pragma once

#include "clientcore/tick/QcSessionCommand.h"
#include "clientcore/tick/QcSessionEvent.h"
#include "common/core/QcMailbox.h"
#include "common/diagnostics/QcRoundTripStats.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>

class QcLinkStats;
class QcSessionCore;
class QcSessionThread;
class QcTickTiming;
class QcTransport;

// The session's side of its tick core (decision E-560): owns the core and, when it ticks on a
// thread of its own, that thread. Commands go to the core and the core's events come back through
// one QcMailbox each (E-010); a queued call only wakes the other side. Without a thread (manual
// ticks, the timer in the session's thread, QTCRAFT_SESSION_THREAD=0) both are handed over
// directly. Lives in the session's thread; the core's thread touches only the event mailbox and
// the wake-up call. A transport that keeps its thread (TLS, decision E-872) is never moved: it is
// made in the session thread (QcNetworkClient) and the core joins it there.
class QcSessionLink : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(QcSessionEvent &&event)>;

    // `handler` takes the core's events in the session's thread, in the core's order. With a tick
    // thread it may delete the link (without one the core calls it directly). While it runs a nested event loop, the events that arrive meanwhile wait until it
    // returns (they must not overtake the rest of the batch), so such a loop sees the session stand.
    QcSessionLink(std::unique_ptr<QcTransport> transport, Handler handler);
    // The core ticks on `thread`, where `transport` lives already (made there, never moved); the
    // core is made here and joins the transport in that thread.
    QcSessionLink(std::unique_ptr<QcSessionThread> thread, std::unique_ptr<QcTransport> transport, Handler handler);
    // Stops the core: the commands given before still reach it (a Disconnect still goes out). The
    // connection closes in the core's thread, which ends after its socket's last frames left.
    ~QcSessionLink() override;

    // Whether timer-driven sessions tick on a thread of their own: unless QTCRAFT_SESSION_THREAD=0.
    static bool threadWanted();

    // Starts the core on a thread of its own when `threaded` (only with the timer), else in this
    // thread; a link made with a thread ticks there in any case. A transport that keeps its thread
    // ticks in this thread then (with a warning; it should have been made in a session thread).
    // Commands before start() reach the core at once.
    void start(const QcSessionCommand::Start &start, bool threaded);
    // Hands the command to the core; an intent equal to the one sent last is left out (the game sets
    // it every frame).
    void command(QcSessionCommand &&command);
    // Mirrored commands given so far (QcSessionEvent::Prediction::mirroredApplied counts the same).
    quint64 mirroredGiven() const { return m_mirroredGiven; }
    // One tick of the core, without a thread only (TickMode::Manual).
    bool tick();
    bool threaded() const { return m_thread != nullptr; }
    // Punctuality of the timer-driven ticks; safe to read and reset from this thread.
    QcTickTiming &timing() const;
    // Round trip and traffic of the connection (health line); safe to read from this thread.
    QcLinkStats &linkStats() const;
    // The round trip to the server (decision E-886); safe to read from this thread.
    std::optional<QcRoundTripSummary> latency() const;
    // Forgets the round trips measured so far; safe from this thread.
    void resetLatency();

private:
    void deliver(QcSessionEvent &&event);
    void drainEvents();
    void drainCommands();

    Handler m_handler;
    std::unique_ptr<QcSessionCore> m_core;
    std::unique_ptr<QcSessionThread> m_thread;
    QcMailbox<QcSessionCommand> m_commands;
    QcMailbox<QcSessionEvent> m_events;
    std::optional<QcPlayerInput> m_sentIntent;
    quint64 m_mirroredGiven = 0;
    bool m_draining = false;
};
