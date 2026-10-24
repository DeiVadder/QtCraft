#include "clientcore/tick/QcSessionCore.h"

#include "common/gameplay/QcStatusEffects.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"

#include <QTimer>

#include <tuple>

QcSessionCore::QcSessionCore(std::unique_ptr<QcTransport> transport, Sink sink)
    : m_sink(std::move(sink))
    , m_timer(new QTimer(this))
    , m_pacer(static_cast<qint64>(QcPhysics::tickDuration * 1e9))
{
    m_connection = std::make_unique<QcConnection>(std::move(transport), QcProtocol::Peer::Server,
                                                  [this](QcMessage &&message) { onMessage(std::move(message)); });
    connect(m_connection.get(), &QcConnection::closed, this, &QcSessionCore::onClosed);
    m_clock.start();
    m_now = [this] { return m_clock.nsecsElapsed(); };
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QcSessionCore::onTimer);
}

QcSessionCore::~QcSessionCore()
{
    // Before the members the connection's signals reach (the sink) are gone.
    shutdown();
}

void QcSessionCore::moveAllToThread(QThread *thread)
{
    moveToThread(thread);
    m_connection->moveToThread(thread);
    m_connection->transport().moveToThread(thread);
}

void QcSessionCore::apply(QcSessionCommand &&command)
{
    if (command.isMirrored()) {
        ++m_mirroredApplied;
    }
    if (const auto *start = std::get_if<QcSessionCommand::Start>(&command.what)) {
        this->start(*start);
    } else if (const auto *intent = std::get_if<QcSessionCommand::SetIntent>(&command.what)) {
        m_prediction.setIntent(intent->intent);
    } else if (const auto *flight = std::get_if<QcSessionCommand::SetFlight>(&command.what)) {
        m_gameMode.setFlightWanted(flight->wanted);
    } else if (std::holds_alternative<QcSessionCommand::ResetCorrections>(command.what)) {
        m_prediction.resetCorrections();
    } else if (const auto *send = std::get_if<QcSessionCommand::Send>(&command.what)) {
        if (m_connection) {
            m_connection->send(send->message);
        }
    } else if (const auto *predicted = std::get_if<QcSessionCommand::PredictBlocks>(&command.what)) {
        m_blocks.predict(predicted->sequence, predicted->blocks);
    } else if (std::holds_alternative<QcSessionCommand::Close>(command.what) && m_connection) {
        m_connection->close();
    }
}

void QcSessionCore::start(const QcSessionCommand::Start &start)
{
    m_connection->send(start.hello);
    if (start.timer) {
        m_pacer.start(m_now());
        m_timer->start(static_cast<int>(QcPhysics::tickDuration * 1000.0));
    }
}

void QcSessionCore::onTimer()
{
    runDueTicks();
    if (!m_closed) {
        // Rounded up: a shot a little early would find nothing due and cost a wake-up.
        const qint64 untilNs = m_pacer.untilNextNs(m_now());
        m_timer->start(static_cast<int>((untilNs + 999'999) / 1'000'000));
    }
}

int QcSessionCore::runDueTicks()
{
    if (!m_playing || m_closed) {
        // Nothing would run: the punctuality counts from the first tick that plays.
        m_pacer.start(m_now());
        return 0;
    }

    const int due = m_pacer.take(m_now());
    for (int i = 0; i < due; ++i) {
        tick();
    }
    return due;
}

bool QcSessionCore::tick()
{
    if (!m_playing || m_closed) {
        return false;
    }

    QcSessionEvent::Ticked ticked;
    // A paused server takes no input (decision E-877): the player rests where the server has him.
    if (!m_vitals.isDead() && !m_serverPaused) {
        const std::shared_ptr<const QcContentRegistry> &content = m_blocks.content();
        m_prediction.setSpeedFactor(content ? static_cast<float>(QcStatusEffects::speedFactor(*content, m_vitals.effects())) : 1.0f);
        const QcPlayerInput input = m_prediction.predict(*m_blocks.world(), m_gameMode.flightWanted(), m_gameMode.rules().movement());
        if (!m_prediction.riding().isRiding()) {
            // A landing or a mode without flight ended the flight: the wish follows the state.
            m_gameMode.stepped(m_prediction.motion().flying);
        }
        m_connection->send(QcMsg::PlayerInput{input});
        m_linkStats.inputSent(input.sequence, m_now());
        ticked.predicted = true;
    }

    m_linkStats.setTraffic(m_connection->traffic());
    ticked.after = prediction();
    m_sink({std::move(ticked)});
    return true;
}

void QcSessionCore::onMessage(QcMessage &&message)
{
    if (m_closed) {
        return;
    }

    if (const auto *state = std::get_if<QcMsg::PlayerState>(&message)) {
        reconcile(*state);
        return;
    }
    // Answered here on the tick thread, so the round trip does not wait for a frame (decision E-848).
    if (const auto *ping = std::get_if<QcMsg::Ping>(&message)) {
        m_connection->send(QcMsg::Pong{ping->nonce});
        return;
    }

    // The core needs the blocks only: an invalid message closes the session (QcClientSession), and
    // what changed the session takes from its own mirror.
    std::ignore = m_blocks.handle(message);
    if (const auto *welcome = std::get_if<QcMsg::Welcome>(&message);
        welcome && welcome->restoredView && welcome->protocolVersion == QcProtocol::version) {
        // The session sets the same view; the core must not tick with the old one meanwhile.
        QcPlayerInput intent = m_prediction.intent();
        intent.yaw = welcome->yaw;
        intent.pitch = welcome->pitch;
        m_prediction.setIntent(intent);
    } else if (const auto *mode = std::get_if<QcMsg::GameMode>(&message)) {
        m_gameMode.setMode(mode->mode);
    } else if (const auto *pause = std::get_if<QcMsg::ServerPause>(&message)) {
        m_serverPaused = pause->paused;
    } else if (m_vitals.apply(message) == QcClientVitals::Change::Health && m_vitals.isDead()) {
        // A player comes back from death on the ground.
        m_gameMode.setFlightWanted(false);
    }
    m_sink({QcSessionEvent::Received{std::move(message)}});
}

void QcSessionCore::reconcile(const QcMsg::PlayerState &state)
{
    m_linkStats.acknowledged(state.lastInputSequence, m_now());
    // A player saved while flying flies on after joining (decision E-284).
    if (m_prediction.reconcile(state, m_blocks.world(), m_gameMode.rules().movement()) == QcPrediction::Reconciled::Placed) {
        m_gameMode.setFlightWanted(state.motion.flying);
    }
    if (m_blocks.world()) {
        m_playing = true;
    }
    m_sink({QcSessionEvent::Reconciled{state, prediction()}});
}

void QcSessionCore::onClosed()
{
    if (m_closed) {
        return;
    }

    m_closed = true;
    m_timer->stop();
    m_sink({QcSessionEvent::Closed{m_connection->lastError()}});
}

QcSessionEvent::Prediction QcSessionCore::prediction() const
{
    return {m_prediction, m_gameMode.flightWanted(), m_mirroredApplied};
}

void QcSessionCore::stop()
{
    m_timer->stop();
    m_closed = true;
}

void QcSessionCore::shutdown()
{
    stop();
    m_connection.reset();
}
