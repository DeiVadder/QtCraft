#pragma once

#include "clientcore/player/QcClientGameMode.h"
#include "clientcore/player/QcClientVitals.h"
#include "clientcore/player/QcPrediction.h"
#include "clientcore/player/QcPushBodies.h"
#include "clientcore/session/QcBlockMirror.h"
#include "clientcore/tick/QcLatencyProbe.h"
#include "clientcore/tick/QcLinkStats.h"
#include "clientcore/tick/QcSessionCommand.h"
#include "clientcore/tick/QcSessionEvent.h"
#include "clientcore/tick/QcTickPacer.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>
#include <memory>

class QcConnection;
class QcTransport;
class QThread;
class QTimer;

// The part of a client session that keeps time (decision E-560): the connection to the server, the
// ticks on the 50 ms grid (QcTickPacer) and the local player's prediction with what it needs, its
// own mirror of the blocks (sharing the immutable chunks, with the player's predicted blocks,
// decision E-853) and of the creatures that push him (QcPushBodies, decision E-882), the game
// mode's wish to fly and whether the player is dead. Every tick it predicts and sends the input;
// every message it receives it applies to that state and passes on, with the ticks and
// reconciliations, as one ordered stream of QcSessionEvent to its sink. Lives in the session's tick
// thread (QcSessionLink) or, without one, in the session's thread; every member is used in that
// thread only, except pacer().timing(), linkStats() and latency().
class QcSessionCore : public QObject
{
    Q_OBJECT

public:
    using Sink = std::function<void(QcSessionEvent &&event)>;
    // Nanoseconds on a steady clock; the ticks are paced on it.
    using Clock = std::function<qint64()>;

    // Without a transport the core waits for attach() before it starts.
    QcSessionCore(std::unique_ptr<QcTransport> transport, Sink sink);
    // Shuts down (shutdown()) if that did not happen yet.
    ~QcSessionCore() override;
    Q_DISABLE_COPY_MOVE(QcSessionCore)

    // Takes the connection's transport, in the core's thread; once, before Start, when the core was
    // made without one (a TLS transport made in the core's thread, decision E-872).
    void attach(std::unique_ptr<QcTransport> transport);
    // Moves the core with its connection and transport to `thread`; called in the core's thread
    // (or before the thread runs). Never with a transport that keeps its thread.
    void moveAllToThread(QThread *thread);
    // Whether the connection's transport must stay in the thread it was made in
    // (QcTransport::keepsItsThread).
    bool transportKeepsItsThread() const;
    // Replaces the steady clock (tests); before Start.
    void setClock(Clock clock) { m_now = std::move(clock); }
    void apply(const QcSessionCommand &command);
    // One tick of the local player; false while the world does not play yet or after the close.
    bool tick();
    // Runs the ticks due on the clock (QcTickPacer::take) and returns how many ran; the timer calls
    // it after Start. Until the world plays, the grid starts anew instead: no tick is due then.
    int runDueTicks();
    // The due times of the timer-driven ticks and their punctuality.
    const QcTickPacer &pacer() const { return m_pacer; }
    QcTickPacer &pacer() { return m_pacer; }
    // Round trip of the inputs and bytes of the connection (health line, decision E-834).
    QcLinkStats &linkStats() { return m_linkStats; }
    // The round trip to the server, measured with probes from the ticks (decision E-886).
    QcLatencyProbe &latency() { return m_latency; }
    const QcLatencyProbe &latency() const { return m_latency; }
    // Stops ticking and taking messages; the connection stays (it may leave the thread with the
    // core and close there, so that a TCP socket lingers in a thread that goes on).
    void stop();
    // Stops and closes and destroys the connection.
    void shutdown();

private:
    void start(const QcSessionCommand::Start &start);
    void onTimer();
    void onMessage(QcMessage &&message);
    void reconcile(const QcMsg::PlayerState &state);
    void onClosed();
    QcSessionEvent::Prediction prediction() const;
    // The push of the creatures on the player for the next step in `movement` (decision E-882).
    [[nodiscard]] QcVec3 push(QcMovement movement) const;

    std::unique_ptr<QcConnection> m_connection;
    Sink m_sink;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    Clock m_now;
    QcTickPacer m_pacer;
    QcLinkStats m_linkStats;
    QcLatencyProbe m_latency;
    QcBlockMirror m_blocks;
    QcPrediction m_prediction;
    // The creatures that push the player (decision E-882), and whether they are moved on by the
    // lead of the input (QcPushBodies::extrapolationWanted).
    QcPushBodies m_pushBodies;
    bool m_pushLeads = QcPushBodies::extrapolationWanted();
    QcClientGameMode m_gameMode;
    QcClientVitals m_vitals;
    bool m_playing = false;
    bool m_closed = false;
    // The server's ticks pause (QcMsg::ServerPause, decision E-877): no prediction, no input.
    bool m_serverPaused = false;
    quint64 m_mirroredApplied = 0;
};
