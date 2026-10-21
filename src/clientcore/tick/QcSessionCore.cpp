#include "clientcore/tick/QcSessionCore.h"

#include "clientcore/session/QcClientWorld.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"

#include <QTimer>

QcSessionCore::QcSessionCore(std::unique_ptr<QcTransport> transport, Sink sink)
    : m_sink(std::move(sink))
    , m_timer(new QTimer(this))
    , m_pacer(qint64(QcPhysics::tickDuration * 1e9))
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
    } else if (std::holds_alternative<QcSessionCommand::Close>(command.what) && m_connection) {
        m_connection->close();
    }
}

void QcSessionCore::start(const QcSessionCommand::Start &start)
{
    m_connection->send(start.hello);
    if (start.timer) {
        m_pacer.start(m_now());
        m_timer->start(int(QcPhysics::tickDuration * 1000.0));
    }
}

void QcSessionCore::onTimer()
{
    runDueTicks();
    if (!m_closed) {
        // Rounded up: a shot a little early would find nothing due and cost a wake-up.
        const qint64 untilNs = m_pacer.untilNextNs(m_now());
        m_timer->start(int((untilNs + 999'999) / 1'000'000));
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
    if (!m_vitals.isDead()) {
        const QcPlayerInput input = m_prediction.predict(*m_world, m_gameMode.flightWanted(), m_gameMode.rules().movement());
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

    applyWorld(message);
    if (const auto *welcome = std::get_if<QcMsg::Welcome>(&message);
        welcome && welcome->restoredView && welcome->protocolVersion == QcProtocol::version) {
        // The session sets the same view; the core must not tick with the old one meanwhile.
        QcPlayerInput intent = m_prediction.intent();
        intent.yaw = welcome->yaw;
        intent.pitch = welcome->pitch;
        m_prediction.setIntent(intent);
    } else if (const auto *mode = std::get_if<QcMsg::GameMode>(&message)) {
        m_gameMode.setMode(mode->mode);
    } else if (m_vitals.apply(message) == QcClientVitals::Change::Health && m_vitals.isDead()) {
        // A player comes back from death on the ground.
        m_gameMode.setFlightWanted(false);
    }
    m_sink({QcSessionEvent::Received{std::move(message)}});
}

void QcSessionCore::applyWorld(const QcMessage &message)
{
    if (const auto *sync = std::get_if<QcMsg::ContentSync>(&message)) {
        if (!m_content) {
            m_content = sync->content;
            m_world = std::make_unique<QcClientWorld>(m_content);
        }
    } else if (const auto *chunkData = std::get_if<QcMsg::ChunkData>(&message)) {
        // An invalid chunk closes the session (QcClientSession); it never reaches the prediction.
        if (m_world && QcClientWorld::knowsBlocksOf(*chunkData->chunk, *m_content)) {
            m_world->setChunk(chunkData->chunk);
        }
    } else if (const auto *unload = std::get_if<QcMsg::ChunkUnload>(&message)) {
        if (m_world) {
            m_world->removeChunk(unload->pos);
        }
    } else if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        if (m_world && m_content->isValidBlock(update->block)) {
            m_world->setBlock(update->pos, update->block);
        }
    } else if (const auto *batch = std::get_if<QcMsg::BlockBatch>(&message)) {
        if (m_world) {
            m_world->setBlocks(batch->updates);
        }
    }
}

void QcSessionCore::reconcile(const QcMsg::PlayerState &state)
{
    m_linkStats.acknowledged(state.lastInputSequence, m_now());
    // A player saved while flying flies on after joining (decision E-284).
    if (m_prediction.reconcile(state, m_world.get(), m_gameMode.rules().movement()) == QcPrediction::Reconciled::Placed) {
        m_gameMode.setFlightWanted(state.motion.flying);
    }
    if (m_world) {
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
