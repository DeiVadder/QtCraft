#pragma once

#include "clientcore/tick/QcSessionCommand.h"
#include "clientcore/tick/QcSessionEvent.h"
#include "common/core/QcMailbox.h"

#include <QObject>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

class QcSessionCore;
class QcTickTiming;
class QcTransport;
class QThread;

// The session's side of its tick core (decision E-560): owns the core and, when it ticks on a
// thread of its own, that thread. Commands go to the core and the core's events come back through
// one QcMailbox each (E-010); a queued call only wakes the other side. Without a thread (manual
// ticks, the timer in the session's thread, QTCRAFT_SESSION_THREAD=0) both are handed over
// directly. Lives in the session's thread; the core's thread touches only the event mailbox, the
// wake-up call and, at the end, the hand-back (handBack()).
class QcSessionLink : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(QcSessionEvent &&event)>;

    // `handler` takes the core's events in the session's thread, in the core's order. With a tick
    // thread it may delete the link (without one the core calls it directly). While it runs a nested event loop, the events that arrive meanwhile wait until it
    // returns (they must not overtake the rest of the batch), so such a loop sees the session stand.
    QcSessionLink(std::unique_ptr<QcTransport> transport, Handler handler);
    // Stops the core: the commands given before still reach it (a Disconnect still goes out).
    ~QcSessionLink() override;

    // Whether timer-driven sessions tick on a thread of their own: unless QTCRAFT_SESSION_THREAD=0.
    static bool threadWanted();

    // Starts the core on a thread of its own when `threaded` (only with the timer), else in this
    // thread. Commands before start() reach the core at once.
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

private:
    void deliver(QcSessionEvent &&event);
    void drainEvents();
    void drainCommands();
    void handBack();

    Handler m_handler;
    std::unique_ptr<QcSessionCore> m_core;
    std::unique_ptr<QThread> m_thread;
    QcMailbox<QcSessionCommand> m_commands;
    QcMailbox<QcSessionEvent> m_events;
    std::optional<QcPlayerInput> m_sentIntent;
    quint64 m_mirroredGiven = 0;
    bool m_draining = false;
    // The core's way back to this thread when the link ends (~QcSessionLink, handBack()).
    std::mutex m_handBackMutex;
    std::condition_variable m_handedBackChanged;
    bool m_handedBack = false;
};
